// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Run only against the disposable test API provided by test_offline_sync.py.
#include "Lem.h"
#include "AppStorage.h"
#include <QProcess>
#include "ReportPdf.h"
#include "TestSignature.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QTimer>
#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QNetworkProxy>
#include <QSettings>
#include <QDesktopServices>
#include <QUuid>
#include <algorithm>
#include <functional>
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            qCritical() << "Failed at line" << __LINE__ << ":" << #c;                              \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
static bool until(std::function<bool()> condition, int timeout = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return condition();
}
static bool settled(Lem &c) {
    return until([&] { return !c.busy() && !c.syncing() && !c.refreshing(); });
}
static QJsonObject read(QString path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
static bool write(QString path, QJsonObject o) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(o).toJson()) > 0;
}
class ExportViewer : public QObject {
    Q_OBJECT
public slots:
    void view(const QUrl &) {} // Test PDFs must never launch an external viewer.
};
struct LemReviewTest {
    static QJsonObject persistedReport(const Lem &client, const QString &id) {
        return read(client.cacheDir() + "/cache.json")["reports"].toObject()[id].toObject();
    }
    static QString checkpoint(const Lem &client) {
        return client.cacheDir() + "/cache.json";
    }

    static void cachedReport(Lem &client, QJsonObject report) {
        client.m_current = report;
        client.m_reports[report["id"].toString()] = report;
    }
    static bool legacySelectedCatalogue(Lem &client) {
        const auto original = client.m_current;
        const auto catalogues = client.m_catalogues;
        const auto contracts = client.m_contracts;
        legacyWithoutCatalogue(client);
        client.m_chargers = catalogues[client.m_contract].toArray();
        auto contract = client.m_contracts[0].toObject();
        contract["presentation"] = QJsonObject{{"issuer", "CSMTEC"}, {"revision", 1}, {"client_logos", QJsonArray{}}};
        client.m_contracts[0] = contract;
        const auto resolved = client.withContractPresentation(client.m_current);
        client.m_contracts = contracts; client.m_catalogues = catalogues;
        cachedReport(client, original);
        return resolved["contract_id"].toString() == client.m_contract &&
               resolved["presentation"].toObject()["issuer"] == "CSMTEC";
    }
    static void legacyWithoutCatalogue(Lem &client) {
        auto report = client.m_current;
        report.remove("contract_id");
        report.remove("presentation");
        auto snapshot = report["snapshot"].toObject();
        auto contract = snapshot["contract"].toObject();
        for (const auto &key : {"id", "presentation", "report_logos"}) contract.remove(key);
        snapshot["contract"] = contract; report["snapshot"] = snapshot;
        client.m_current = report;
        client.m_reports[report["id"].toString()] = report;
        client.m_catalogues = {};
        client.m_chargers = {};
    }
    static bool failedCorrectionCheckpoint(Lem &client) {
        auto draft = client.m_current;
        draft["status"] = "closed";
        draft["correction"] = QJsonObject{{"state", "editing"}, {"editor_id", client.m_user["id"]}, {"source_id", "source-test"}};
        draft["_conflict"] = true;
        draft["_conflict_kind"] = "station_identity_conflict";
        draft["_conflict_reason"] = "A ficha do carregador mudou.";
        client.m_current = draft; client.remember();
        auto saved = draft;
        saved["revision"] = 2;
        saved["owner_id"] = "original-technician";
        auto correction = saved["correction"].toObject(); correction["state"] = "committed";
        saved["correction"] = correction;
        const auto cache = client.cacheDir() + "/cache.json";
        // Disposable fixture only: make the destination unwritable without touching evidence.
        if (!QFile::rename(cache, cache + ".saved") || !QDir().mkdir(cache)) return false;
        const bool rejected = !client.mergeSync(saved, true);
        const bool recoverable = client.m_current["owner_id"] == client.m_user["id"] &&
            client.m_current["correction"].toObject()["state"] == "editing" &&
            client.m_current["dirty"].toBool() && client.m_current["_conflict"].toBool() &&
            client.m_current["_conflict_kind"] == "station_identity_conflict";
        if (!QDir().rmdir(cache) || !QFile::rename(cache + ".saved", cache)) return false;
        return rejected && recoverable && client.mergeSync(saved, true) &&
            !client.m_current["dirty"].toBool() && !client.m_current["_conflict"].toBool() &&
            client.m_current["owner_id"] == "original-technician";
    }
    static void staleForeignDraft(Lem &client) {
        auto r = client.m_current;
        r["dirty"] = true;
        r["status"] = "draft";
        r["payload"] = QJsonObject{{"answers", QJsonObject{{"SITE.02", QJsonObject{{"value", "NOK"}, {"notes", "cópia estrangeira protegida"}}}}}};
        client.m_current = r;
        client.m_reports[r["id"].toString()] = r;
        emit client.changed();
    }
};

static bool completeAnswers(Lem &client, const QString &source) {
    for (auto section : client.sections())
        for (auto entry : section.toMap()["items"].toList()) {
            const auto item = entry.toMap();
            const auto key = item["id"].toString();
            client.answer(key, item["kind"] == "notes" ? "" : "NA", "Sem conector/veículo disponível");
            if (item["photo_always"].toBool()) client.attach(key, QUrl::fromLocalFile(source));
            if (item["signature_required"].toBool()) {
                client.answer(key, "Técnico de teste", "");
                QFile png(source);
                if (!png.open(QIODevice::ReadOnly) ||
                    !client.saveSignature(key, "data:image/png;base64," + QString::fromLatin1(png.readAll().toBase64())))
                    return false;
            }
        }
    return true;
}

static QString notes(Lem &client, const QString &key) {
    return client.current()["payload"].toMap()["answers"].toMap()[key].toMap()["notes"].toString();
}

static int exerciseDeleteAck(Lem &client, const QString &charger, const QString &source, const QString &state) {
    const auto attach = [&] {
        client.attach("SITE.02", QUrl::fromLocalFile(source));
        return client.current()["photos"].toList().last().toMap()["id"].toString();
    };
    const auto sync = [&] {
        client.sync();
        return settled(client);
    };

    client.openReport(charger);
    CHECK(completeAnswers(client, source));
    client.answer("FIN.03", "", "ORIGINAL");
    CHECK(sync() && !client.current()["dirty"].toBool());
    const auto racedReport = client.current()["id"].toString();
    const auto base = client.current()["revision"];
    client.removePhoto(attach()); // Never uploaded. A same-base absence is a successful no-op.
    CHECK(sync() && !client.current()["dirty"].toBool());
    CHECK(client.current()["revision"] == base && client.current()["deleted_photos"].toList().isEmpty());

    client.answer("FIN.03", "", "RACE_DELETE_ACK");
    CHECK(sync() && !client.current()["dirty"].toBool());
    const auto raceBase = client.current()["revision"];
    client.removePhoto(attach()); // The API fixture schedules B's PUT immediately before DELETE.
    client.sync();
    CHECK(client.syncing());
    // Keep a newer local edit pending so a catalogue refresh cannot update the
    // cached answers before the next sync attempt exercises the stale revision.
    client.answer("SITE.02", "NA", "A edits another answer");
    CHECK(settled(client));
    const bool protectedBase = client.current()["_conflict"].toBool() &&
        client.current()["revision"] == raceBase && client.current()["deleted_photos"].toList().size() == 1;
    // The earlier client actually uploads this unrelated answer and closes over B's answers.
    CHECK(sync());
    client.finish();
    CHECK(sync());
    qInfo() << "Absent-photo race:" << protectedBase << client.current()["revision"] << client.current()["status"];
    CHECK(protectedBase);
    CHECK(client.current()["status"] == "draft" && notes(client, "FIN.03") == "RACE_DELETE_ACK");
    CHECK(notes(client, "SITE.02") == "A edits another answer");

    client.openReport(charger);
    client.answer("FIN.03", "", "DROP_UPLOAD_ACK");
    const auto lostUpload = attach();
    CHECK(sync() && client.current()["dirty"].toBool());
    CHECK(!client.current()["photos"].toList().first().toMap()["uploaded"].toBool());
    client.removePhoto(lostUpload);
    CHECK(sync() && !client.current()["dirty"].toBool());
    CHECK(client.current()["photos"].toList().isEmpty() && client.current()["deleted_photos"].toList().isEmpty());

    client.openReport(charger);
    CHECK(completeAnswers(client, source));
    client.answer("FIN.03", "", "DROP_DELETE_ACK");
    const auto lostDelete = attach();
    CHECK(sync() && !client.current()["dirty"].toBool());
    const auto lostBase = client.current()["revision"].toInt();
    client.removePhoto(lostDelete);
    CHECK(sync() && client.current()["dirty"].toBool() && !client.current()["_conflict"].toBool());
    CHECK(client.current()["_inflight_delete_id"] == lostDelete);
    client.answer("FIN.03", "", "Newer local answer after lost response");
    CHECK(write(state + "/resume.json", {
        {"race_id", racedReport}, {"id", client.current()["id"].toString()},
        {"photo_id", lostDelete}, {"base", lostBase},
    }));
    qInfo() << "PASS: same-base no-op, interleaved answer protection, lost upload response and durable DELETE intent";
    return 0;
}

static int resumeDeleteAck(const QString &state) {
    const auto fixture = read(state + "/resume.json");
    Lem client(state + "/device");
    client.setServer("http://127.0.0.1:18765");
    client.login("admin@lem.pt", "IntegrationPassword123!");
    CHECK(settled(client) && client.authenticated());
    client.openReport("", fixture["race_id"].toString());
    CHECK(settled(client) && client.current()["_conflict"].toBool());
    CHECK(client.current()["deleted_photos"].toList().size() == 1);
    client.openReport("", fixture["id"].toString());
    CHECK(settled(client) && client.current()["dirty"].toBool());
    CHECK(client.current()["_inflight_delete_id"].toString() == fixture["photo_id"].toString());
    CHECK(notes(client, "FIN.03") == "Newer local answer after lost response");
    CHECK(client.current()["revision"].toInt() == fixture["base"].toInt());

    client.sync();
    CHECK(settled(client) && !client.current()["dirty"].toBool() && !client.current()["_conflict"].toBool());
    CHECK(client.current()["deleted_photos"].toList().isEmpty() && !client.current().contains("_inflight_delete_id"));
    CHECK(client.current()["revision"].toInt() == fixture["base"].toInt() + 2);
    client.finish();
    CHECK(client.current()["status"] == "closed");
    client.sync();
    CHECK(settled(client) && !client.current()["dirty"].toBool());
    CHECK(notes(client, "FIN.03") == "Newer local answer after lost response");
    qInfo() << "PASS: process restart recovered the committed DELETE, kept newer local edits and closed";
    return 0;
}

// The API fixture removes and restores this technician's grant between two launches.
static int restoredVisit(const QStringList &args) {
    const auto endpoint = args[2], root = args[3], order = args[4], item = args[5], target = args[6], id = args[7];
    const auto open = [&](Lem &client) {
        client.setServer(endpoint);
        client.login("old-shift@example.invalid", qEnvironmentVariable("LEM_ORDER_TEST_PASSWORD"));
        return until([&]{ return client.authenticated() && !client.workOrders().isEmpty() && !client.refreshing(); })
            && client.prepareOrderReport(order, item) == id && client.openReport(target, id)
            && until([&]{ return client.current()["id"] == id && !client.busy(); });
    };
    const auto expected = QStringLiteral("Ângulo revisto, ligação confirmada / field follow-up.");
    {
        Lem field(root + "/profile");
        CHECK(open(field));
        CHECK(field.current()["_order_item"] == item);
        CHECK(write(root + "/opened", {}));
        CHECK(until([&]{ return QFile::exists(root + "/scope-removed"); }));
        field.answer("FIN.03", "", expected);
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        CHECK(image.save(root + "/original.png"));
        field.attach("FIN.03", QUrl::fromLocalFile(root + "/original.png"));
        field.sync();
        CHECK(until([&]{ return field.current()["_conflict"].toBool() && !field.syncing(); }));
        CHECK(field.current()["_conflict_kind"] == "order_scope" && notes(field, "FIN.03") == expected);
        CHECK(write(root + "/upload-rejected", {}));
        CHECK(until([&]{ return QFile::exists(root + "/scope-restored"); }));
    }
    Lem restarted(root + "/profile");
    CHECK(open(restarted));
    CHECK(restarted.current()["_order_item"] == item);
    restarted.sync();
    if (!until([&]{ return !restarted.syncing() && !restarted.current()["dirty"].toBool(); })) {
        qCritical() << "Restored visit did not synchronize:" << restarted.syncState() << restarted.message()
                    << restarted.current()["_conflict_reason"] << restarted.current()["_contract_unavailable"];
        return 1;
    }
    CHECK(restarted.current()["id"] == id && !restarted.current()["_conflict"].toBool());
    CHECK(notes(restarted, "FIN.03") == expected);
    const auto photo = restarted.current()["photos"].toList().value(0).toMap();
    QFile original(root + "/original.png");
    CHECK(original.open(QIODevice::ReadOnly));
    const auto digest = QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256).toHex();
    CHECK(photo["uploaded"].toBool() && photo["digest"].toString() == QString::fromLatin1(digest));
    qInfo() << "Restored visit, same UUID and evidence SHA-256" << digest;
    return 0;
}

static int deferredOrderStart(const QStringList &args) {
    const auto server = args[2], root = args[3], target = args[4], scenario = args[5];
    const bool grantRestored = scenario == "individual" || scenario == "legacy-individual";
    const auto profile = root + "/profile", originalPath = root + "/original.png";
    const auto observation = QStringLiteral("Ligação revista na Âncora / inspected on site.");
    QString id, checkpoint;
    const auto login = [&](Lem &client) {
        client.setServer(server);
        client.login("pending-visit@example.invalid", qEnvironmentVariable("LEM_ORDER_TEST_PASSWORD"));
        return until([&] {
            if (!client.authenticated() || client.busy() || client.refreshing()) return false;
            const auto jobs = client.interventions();
            return std::any_of(jobs.cbegin(), jobs.cend(),
                [&](const auto &job) { return job.toMap()["id"] == target; });
        });
    };
    {
        Lem client(profile);
        CHECK(login(client));
        CHECK(write(root + "/opened", {}));
        CHECK(until([&] { return QFile::exists(root + "/offline"); }));
        CHECK(client.prepareIntervention(target).isEmpty() && client.openReport(target));
        id = client.current()["id"].toString();
        client.answer("FIN.03", "", observation);
        QImage original(80, 60, QImage::Format_RGB32);
        original.fill(Qt::darkGreen);
        CHECK(original.save(originalPath));
        client.attach("FIN.03", QUrl::fromLocalFile(originalPath));
        CHECK(until([&] { return !client.saving() && client.current()["photos"].toList().size() == 1; }));
        CHECK(write(root + "/saved", {{"id", id}}));
        CHECK(until([&] { return QFile::exists(root + "/planned"); }));
        client.sync();
        CHECK(until([&] {
            const auto current = client.current();
            return !client.syncing() && (grantRestored ? current["_conflict_kind"] == "order_scope"
                                                       : current["_order_start_retry_at"].toDouble() > 0);
        }));
        CHECK(client.current()["_conflict"].toBool() == grantRestored && notes(client, "FIN.03") == observation);
        const auto saved = LemReviewTest::persistedReport(client, id);
        if (grantRestored)
            CHECK(saved["_conflict_kind"] == "order_scope" && saved["_order_item"].toString().isEmpty());
        else
            CHECK(saved["_order_start_retry_at"].toInteger() > 0);
        checkpoint = LemReviewTest::checkpoint(client);
        CHECK(write(root + "/deferred", {}));
        CHECK(until([&] { return QFile::exists(root + "/unblocked"); }));
    }
    if (scenario == "legacy" || scenario == "legacy-individual") {
        // An installed client may still have a refusal without its retry classification.
        auto cache = read(checkpoint);
        auto reports = cache["reports"].toObject();
        auto old = reports[id].toObject();
        old.remove("_order_start_retry_at");
        old.remove("_conflict_kind");
        old["_conflict"] = true;
        old["_conflict_reason"] = grantRestored ? "Contrato ou tarefa não atribuídos para esta operação."
                                               : "A ordem desta intervenção ainda não começou.";
        reports[id] = old;
        cache["reports"] = reports;
        CHECK(write(checkpoint, cache));
    }
    Lem restarted(profile);
    CHECK(login(restarted));
    CHECK(restarted.prepareIntervention(target) == id && restarted.openReport(target, id));
    if (scenario == "legacy" || scenario == "stale" || scenario == "legacy-individual")
        restarted.sync();
    if (scenario == "stale") {
        CHECK(until([&] { return !restarted.syncing() && restarted.current()["_conflict"].toBool(); }));
        CHECK(notes(restarted, "FIN.03") == observation);
        CHECK(!restarted.current().contains("_order_start_retry_at"));
    } else {
        CHECK(until([&] { return !restarted.syncing() && !restarted.current()["dirty"].toBool(); }, 45000));
        CHECK(restarted.current()["id"] == id && notes(restarted, "FIN.03") == observation);
        CHECK(!restarted.current()["_conflict"].toBool() && !restarted.current().contains("_order_start_retry_at"));
        CHECK(restarted.current()["photos"].toList().value(0).toMap()["uploaded"].toBool());
    }
    qInfo() << "Checked deferred order start" << scenario << "for" << id;
    return 0;
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    ExportViewer exportViewer;
    QDesktopServices::setUrlHandler("file", &exportViewer, "view");
    // These integration fixtures only contact the disposable loopback server.
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    app.setApplicationVersion(qEnvironmentVariable("LEM_TEST_CLIENT_VERSION", "0.9.0"));
    app.setOrganizationName("Lem-Offline-Test");
    app.setApplicationName("Lem Offline Test");
    const auto args = app.arguments();
    const auto state = args.contains("--state") ? args.value(args.indexOf("--state") + 1) : QString{};
    QTemporaryDir temp;
    // Both processes use the same disposable settings, including the server URL.
    const auto settingsRoot = state.isEmpty() ? temp.path() : state;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot + "/settings");
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsRoot + "/system-settings");
    if (args.size() == 3 && args[1] == "--second-profile-writer") {
        const auto lock = AppStorage::lockWriter(args[2]);
        if (!lock) return 3;
        Lem second(args[2]);
        second.demo();
        second.answer("FIN.03", "", "Segunda intervenção / South");
        CHECK(until([&] { return !second.saving(); }));
        CHECK(write(args[2] + "/second-draft.json", {{"id", second.current()["id"].toString()}}));
        return 0;
    }
    if (args.size() == 1) {
        const auto profile = temp.filePath("shared-profile");
        const QStringList second{"--second-profile-writer", profile};
        QString firstId;
        {
            const auto writer = AppStorage::lockWriter(profile);
            CHECK(writer);
            Lem technician(profile);
            technician.demo();
            firstId = technician.current()["id"].toString();
            technician.answer("SITE.02", "NOK", "Medição Âncora / North");
            CHECK(until([&] { return !technician.saving(); }));
            CHECK(QProcess::execute(app.applicationFilePath(), second) == 3);
        }
        CHECK(QProcess::execute(app.applicationFilePath(), second) == 0);
        const auto secondId = read(profile + "/second-draft.json")["id"].toString();
        CHECK(!firstId.isEmpty() && !secondId.isEmpty() && firstId != secondId);
        const auto writer = AppStorage::lockWriter(profile);
        CHECK(writer);
        Lem reloaded(profile);
        reloaded.demo();
        CHECK(reloaded.openReport("", firstId) && notes(reloaded, "SITE.02") == "Medição Âncora / North");
        CHECK(reloaded.openReport("", secondId) && notes(reloaded, "FIN.03") == "Segunda intervenção / South");
        qInfo() << "PASS: two desktop processes and a restarted controller retained both offline drafts";
    }
    if (args.size() == 8 && args[1] == "--restored-visit") return restoredVisit(args);
    if (args.size() == 6 && args[1] == "--order-start-retry") return deferredOrderStart(args);
    if (args.size() == 6 && args[1] == "--work-order-download") {
        Lem observer(temp.path() + "/observer");
        observer.setServer(args[2]);
        observer.login("admin@lem.pt", qEnvironmentVariable("LEM_ORDER_TEST_PASSWORD"));
        CHECK(until([&] { return observer.authenticated() && observer.workOrders().size() == 2; }));
        // No navigation or manual download: verify the durable copy, original bytes
        // and absence of the other administrator's unsubscribed report.
        const QString followed = args[3], ignored = args[4];
        CHECK(until([&] {
            const auto report = LemReviewTest::persistedReport(observer, followed);
            const auto files = report["photos"].toArray();
            if (report["status"] != "closed" || !report["offline_photos"].toBool() || files.size() != 1)
                return false;
            const auto path = QUrl(files[0].toObject()["path"].toString()).toLocalFile();
            if (path.isEmpty()) return false;
            QFile evidence(path);
            return evidence.open(QIODevice::ReadOnly) &&
                   QCryptographicHash::hash(evidence.readAll(), QCryptographicHash::Sha256).toHex() == args[5].toLatin1();
        }));
        CHECK(observer.current().value("id").toString().isEmpty());
        CHECK(LemReviewTest::persistedReport(observer, ignored).isEmpty());
        qInfo() << "PASS followed report and verified original received automatically";
        return 0;
    }
    if (args.contains("--resume-delete-ack")) return resumeDeleteAck(state);
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    const auto source = temp.path() + "/original.png";
    CHECK(image.save(source));
    {
        Lem c(temp.path() + "/journal");
        c.demo();
        CHECK(LemReviewTest::legacySelectedCatalogue(c));
        c.attach("EST.01", QUrl::fromLocalFile(source));
        const auto rid = c.current()["id"].toString();
        const auto photo = c.current()["photos"].toList().first().toMap()["path"].toString();
        const auto dir =
            QDir::cleanPath(QFileInfo(QUrl(photo).toLocalFile()).absolutePath() + "/..");
        const auto cache = dir + "/cache.json";
        QFile old(cache);
        CHECK(old.open(QIODevice::ReadOnly));
        const auto before = old.readAll();
        old.close();
        for (int i = 0; i < 50; ++i)
            c.answer("SITE.02", "NOK", "Nota " + QString::number(i));
        CHECK(until([&] { return !c.saving(); }));
        CHECK(QDir(dir + "/edits").entryList({"*.json"}, QDir::Files).size() == 1);
        QFile same(cache);
        CHECK(same.open(QIODevice::ReadOnly));
        CHECK(same.readAll() == before);
        same.close();
        // A cold offline launch must expire only old cancellations and replay the newer draft journal.
        const auto clock = QDateTime::currentDateTimeUtc();
        const QJsonObject activeOrder{{"id", "active-order"}};
        const QJsonObject recentOrder{{"id", "recent-cancellation"},
                                      {"cancelled_at", clock.addSecs(-23 * 3600).toString(Qt::ISODateWithMs)}};
        const QJsonObject expiredOrder{{"id", "expired-cancellation"},
                                       {"cancelled_at", clock.addSecs(-25 * 3600).toString(Qt::ISODateWithMs)}};
        auto orderCache = read(cache);
        orderCache["work_orders"] = QJsonArray{expiredOrder, recentOrder, activeOrder};
        CHECK(write(cache, orderCache));
        Lem restored(temp.path() + "/journal");
        restored.demo();
        const QJsonArray retained{recentOrder, activeOrder};
        CHECK(restored.workOrders() == retained.toVariantList());
        CHECK(read(cache)["work_orders"].toArray() == retained);
        restored.openReport("", rid);
        CHECK(
            restored.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
            "Nota 49");
        // An acknowledged closed report is eligible for storage eviction; a draft is not.
        auto manifest = read(cache);
        auto reports = manifest["reports"].toObject();
        auto closed = QJsonObject::fromVariantMap(restored.current());
        CHECK(closed["_defer_photo_sync"].toBool());
        for (const auto &entry : QDir(dir + "/edits").entryList({"*.json"}, QDir::Files))
            CHECK(QFile::remove(dir + "/edits/" + entry));
        closed["status"] = "closed";
        closed["revision"] = 1; // This fixture represents server-confirmed evidence.
        closed["dirty"] = false;
        closed["last_access"] = 0;
        auto photos = closed["photos"].toArray();
        auto p = photos[0].toObject();
        p["uploaded"] = true;
        photos[0] = p;
        closed["photos"] = photos;
        reports[rid] = closed;
        manifest["reports"] = reports;
        CHECK(write(cache, manifest));
        const auto pinnedId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto pinnedPhoto = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto pinnedPath = dir + "/photos/" + pinnedPhoto + ".png";
        QFile pinnedFile(pinnedPath);
        CHECK(pinnedFile.open(QIODevice::WriteOnly));
        CHECK(pinnedFile.resize(257LL * 1024 * 1024));
        pinnedFile.close();
        auto pinned = closed;
        pinned["id"] = pinnedId;
        pinned["offline_photos"] = true;
        auto pp = p;
        pp["id"] = pinnedPhoto;
        pp["path"] = QUrl::fromLocalFile(pinnedPath).toString();
        pinned["photos"] = QJsonArray{pp};
        reports[pinnedId] = pinned;
        manifest["reports"] = reports;
        CHECK(write(cache, manifest));
        Lem cleaner(temp.path() + "/journal");
        cleaner.demo();
        CHECK(!QFileInfo::exists(QUrl(photo).toLocalFile()));
        CHECK(QFileInfo::exists(source));
        CHECK(QFileInfo::exists(pinnedPath));
        cleaner.openReport("", pinnedId);
        CHECK(cleaner.current()["offline_photos"].toBool());
        cleaner.releaseReportPhotos();
        CHECK(!cleaner.current()["offline_photos"].toBool());
        cleaner.openReport("demo-charger");
        cleaner.cleanPhotoCache();
        CHECK(!QFileInfo::exists(pinnedPath));
        cleaner.openReport("", rid);
        CHECK(cleaner.current()["photos"].toList().size() == 1);
        CHECK(cleaner.current()["photos"].toList().first().toMap()["path"].toString().isEmpty());
    }
    {
        Lem c(temp.path() + "/protected");
        c.demo();
        c.attach("EST.01", QUrl::fromLocalFile(source));
        const auto rid = c.current()["id"].toString();
        const auto path =
            QUrl(c.current()["photos"].toList().first().toMap()["path"].toString()).toLocalFile();
        // Sparse fixture: exercise the 256 MiB budget without allocating an image in RAM.
        QFile large(path);
        CHECK(large.open(QIODevice::WriteOnly));
        CHECK(large.resize(257LL * 1024 * 1024));
        large.close();
        c.cleanPhotoCache();
        CHECK(QFileInfo::exists(path));
        CHECK(c.photoCacheMB() > 256);
        Lem restored(temp.path() + "/protected");
        CHECK(!restored.authenticated());
        restored.demo();
        CHECK(!restored.authenticated());
        restored.openReport("", rid);
        CHECK(QFileInfo::exists(path));
        CHECK(restored.current()["photos"].toList().size() == 1);
    }
    {
        Lem c(temp.path() + "/slow-storage");
        c.demo();
        int templateSignals = 0;
        QObject::connect(&c, &Lem::sectionsChanged, &app, [&] { ++templateSignals; });
        qputenv("LEM_TEST_WRITE_DELAY_MS", "500");
        QElapsedTimer elapsed;
        elapsed.start();
        c.answer("SITE.02", "OK", "primeira");
        const auto firstMs = elapsed.elapsed();
        CHECK(firstMs < 100);
        CHECK(c.saving());
        // Start the deliberately slow atomic write, then edit again during that write.
        QElapsedTimer running;
        running.start();
        CHECK(until([&] { return running.elapsed() > 50; }, 1000));
        elapsed.restart();
        c.answer("SITE.02", "NOK", "última");
        const auto secondMs = elapsed.elapsed();
        CHECK(secondMs < 100);
        int heartbeats = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeats; });
        heartbeat.start();
        CHECK(until([&] { return !c.saving(); }, 4000));
        heartbeat.stop();
        CHECK(heartbeats > 30);
        CHECK(!c.localSaveError());
        CHECK(templateSignals == 0);
        qunsetenv("LEM_TEST_WRITE_DELAY_MS");
        const auto rid = c.current()["id"].toString();
        Lem restored(temp.path() + "/slow-storage");
        restored.demo();
        restored.openReport("", rid);
        CHECK(
            restored.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
            "última");
        qInfo() << "PASS: 500 ms storage write; answer calls (ms)" << firstMs << secondMs
                << "; event-loop ticks" << heartbeats
                << "; latest edit durable; template model unchanged";
    }
    {
        const auto path = temp.path() + "/shutdown";
        QString rid;
        {
            Lem c(path);
            c.demo();
            rid = c.current()["id"].toString();
            c.answer("SITE.02", "OK", "última ao sair");
            CHECK(c.saving());
        }
        Lem restored(path);
        restored.demo();
        restored.openReport("", rid);
        CHECK(
            restored.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
            "última ao sair");
    }
    {
        Lem client(temp.path() + "/correction-checkpoint");
        client.demo();
        CHECK(LemReviewTest::failedCorrectionCheckpoint(client));
        qInfo() << "PASS: failed correction checkpoint retains editor ownership for recovery; retry durably restores technician owner";
    }
    {
        Lem c(temp.path() + "/write-failure");
        c.demo();
        c.attach("EST.01", QUrl::fromLocalFile(source));
        const auto photo = c.current()["photos"].toList()[0].toMap()["path"].toString();
        const auto dir =
            QDir::cleanPath(QFileInfo(QUrl(photo).toLocalFile()).absolutePath() + "/../edits");
        const auto names = QDir(dir).entryList({"*.json"}, QDir::Files);
        CHECK(names.size() == 1);
        const auto journal = dir + "/" + names.first();
        CHECK(QFile::remove(journal));
        CHECK(QDir().mkdir(journal));
        c.answer("SITE.02", "NOK", "preservar após falha");
        CHECK(until([&] { return c.localSaveError(); }, 2000));
        CHECK(c.saving());
        CHECK(c.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
              "preservar após falha");
        CHECK(QDir().rmdir(journal));
        CHECK(until([&] { return !c.saving(); }, 4000));
        CHECK(!c.localSaveError());
        const auto rid = c.current()["id"].toString();
        Lem restored(temp.path() + "/write-failure");
        restored.demo();
        restored.openReport("", rid);
        CHECK(
            restored.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
            "preservar após falha");
    }
    {
        const auto path = temp.path() + "/delete-local";
        Lem c(path);
        c.demo();
        c.attach("EST.01", QUrl::fromLocalFile(source));
        c.answer("SITE.02", "OK", "apagar");
        const auto rid = c.current()["id"].toString();
        const auto stale = QJsonObject::fromVariantMap(c.current());
        const auto file =
            QUrl(c.current()["photos"].toList()[0].toMap()["path"].toString()).toLocalFile();
        const auto dir = QDir::cleanPath(QFileInfo(file).absolutePath() + "/..");
        c.deleteReport(rid);
        CHECK(c.current().isEmpty());
        CHECK(!QFileInfo::exists(file));
        CHECK(QFileInfo::exists(source));
        // Even a stale journal left by a crash must not restore a deleted draft.
        CHECK(write(dir + "/edits/stale.json", stale));
        Lem restored(path);
        restored.demo();
        restored.openReport("", rid);
        CHECK(restored.current()["id"].toString() != rid);
    }
    {
        // Explicit cleanup ignores age, budget, pin and current report, but preserves unsent work.
        const auto path = temp.path() + "/manual-clean";
        Lem c(path);
        c.demo();
        c.attach("EST.01", QUrl::fromLocalFile(source));
        const auto closedId = c.current()["id"].toString();
        const auto photo =
            QUrl(c.current()["photos"].toList()[0].toMap()["path"].toString()).toLocalFile();
        const auto dir = QDir::cleanPath(QFileInfo(photo).absolutePath() + "/..");
        auto manifest = read(dir + "/cache.json");
        auto reports = manifest["reports"].toObject();
        auto closed = QJsonObject::fromVariantMap(c.current());
        closed["status"] = "closed";
        closed["dirty"] = false;
        closed["revision"] = 2;
        closed["offline_photos"] = true;
        closed["last_access"] = double(QDateTime::currentSecsSinceEpoch());
        auto photos = closed["photos"].toArray();
        auto p = photos[0].toObject();
        p["uploaded"] = true;
        photos[0] = p;
        closed["photos"] = photos;
        reports[closedId] = closed;
        auto extraClosed = closed;
        extraClosed["id"] = "ffffffff-ffff-ffff-ffff-ffffffffffff";
        extraClosed["photos"] = QJsonArray{};
        reports[extraClosed["id"].toString()] = extraClosed;
        const auto draftId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto draft = closed;
        draft["id"] = draftId;
        draft["status"] = "draft";
        draft["dirty"] = true;
        const auto draftPhoto = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto draftPath = dir + "/photos/" + draftPhoto + ".png";
        CHECK(QFile::copy(photo, draftPath));
        auto dp = p;
        dp["id"] = draftPhoto;
        dp["uploaded"] = false;
        dp["path"] = QUrl::fromLocalFile(draftPath).toString();
        draft["photos"] = QJsonArray{dp};
        reports[draftId] = draft;
        const auto dirtyId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto dirty = draft;
        dirty["id"] = dirtyId;
        dirty["status"] = "closed";
        reports[dirtyId] = dirty;
        const auto syncedDraftId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto syncedDraft = closed;
        syncedDraft["id"] = syncedDraftId;
        syncedDraft["status"] = "draft";
        const auto syncedPhotoId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto syncedPhotoPath = dir + "/photos/" + syncedPhotoId + ".png";
        CHECK(QFile::copy(photo, syncedPhotoPath));
        auto sp = p;
        sp["id"] = syncedPhotoId;
        sp["path"] = QUrl::fromLocalFile(syncedPhotoPath).toString();
        syncedDraft["photos"] = QJsonArray{sp};
        reports[syncedDraftId] = syncedDraft;
        const auto conflictId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto conflict = syncedDraft;
        conflict["id"] = conflictId;
        conflict["_conflict"] = true;
        conflict["photos"] = QJsonArray{};
        reports[conflictId] = conflict;
        const auto deletionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto deletion = conflict;
        deletion["id"] = deletionId;
        deletion.remove("_conflict");
        deletion["deleted_photos"] = QJsonArray{syncedPhotoId};
        reports[deletionId] = deletion;
        manifest["reports"] = reports;
        CHECK(write(dir + "/cache.json", manifest));
        for (const auto &name : QDir(dir + "/edits").entryList({"*.json"}, QDir::Files))
            CHECK(QFile::remove(dir + "/edits/" + name));
        Lem cleaner(path);
        cleaner.demo();
        cleaner.openReport("", closedId);
        CHECK(cleaner.current()["offline_photos"].toBool());
        cleaner.clearLocalCopies();
        CHECK(cleaner.current()["id"].toString() == closedId);
        CHECK(!QFileInfo::exists(photo));
        CHECK(!QFileInfo::exists(syncedPhotoPath));
        const auto cleanedReports = read(dir + "/cache.json")["reports"].toObject();
        CHECK(cleanedReports.contains(syncedDraftId));
        CHECK(cleanedReports.contains(conflictId) && cleanedReports.contains(deletionId));
        CHECK(QFileInfo::exists(draftPath));
        CHECK(QFileInfo::exists(source));
        cleaner.openReport("", draftId);
        CHECK(cleaner.current()["photos"].toList().size() == 1);
        cleaner.openReport("", dirtyId);
        CHECK(cleaner.current()["id"].toString() == dirtyId);
        Lem restored(path);
        restored.demo();
        restored.openReport("", closedId);
        CHECK(restored.current()["id"].toString() == closedId);
        restored.openReport("", draftId);
        CHECK(restored.current()["id"].toString() == draftId);
    }
    {
        // Closed offline work stays protected through restart; copies get independent
        // IDs, dates and files, and cannot accidentally remove the source's images.
        const auto root = temp.path() + "/conflict-copy";
        QString originalId, originalPhoto, originalPath, copyId, copyPath;
        {
            Lem c(root);
            c.demo();
            c.attach("EST.01", QUrl::fromLocalFile(source));
            c.answer("SITE.02", "NOK", "Preservar esta observação");
            CHECK(until([&] { return !c.saving(); }));
            originalId = c.current()["id"].toString();
            auto r = QJsonObject::fromVariantMap(c.current());
            auto p = r["photos"].toArray()[0].toObject();
            originalPhoto = p["id"].toString();
            originalPath = QUrl(p["path"].toString()).toLocalFile();
            const auto cache = QFileInfo(originalPath).absolutePath() + "/../cache.json";
            auto manifest = read(cache);
            auto reports = manifest["reports"].toObject();
            r["_conflict"] = true;
            r["_conflict_reason"] = "Propriedade transferida";
            r["status"] = "closed";
            r["started"] = "2020-01-01T00:00:00Z";
            r["finished"] = "2020-01-02T00:00:00Z";
            auto payload = r["payload"].toObject();
            payload["finished"] = "2020-01-02T00:00:00Z";
            r["payload"] = payload;
            reports[originalId] = r;
            manifest["reports"] = reports;
            CHECK(write(cache, manifest));
        }
        {
            Lem restored(root);
            restored.demo();
            restored.openReport("", originalId);
            CHECK(restored.current()["_conflict"].toBool());
            const auto before = restored.current()["payload"];
            restored.answer("SITE.02", "OK", "não alterar");
            CHECK(restored.current()["payload"] == before);
            restored.copyLocalReport();
            CHECK(settled(restored));
            copyId = restored.current()["id"].toString();
            CHECK(copyId != originalId);
            CHECK(restored.current()["status"] == "draft" && restored.current()["dirty"].toBool() &&
                  restored.current()["revision"].toInt() == 0);
            CHECK(!restored.current()["_conflict"].toBool());
            CHECK(!restored.current().contains("finished"));
            CHECK(restored.current()["started"] != "2020-01-01T00:00:00Z");
            const auto payload = restored.current()["payload"].toMap();
            CHECK(payload["source_report_id"] == originalId);
            CHECK(!payload.contains("finished"));
            CHECK(payload["answers"].toMap()["SITE.02"].toMap()["notes"] ==
                  "Preservar esta observação");
            auto p = restored.current()["photos"].toList()[0].toMap();
            CHECK(p["id"].toString() != originalPhoto && !p["uploaded"].toBool());
            copyPath = QUrl(p["path"].toString()).toLocalFile();
            QFile a(originalPath), b(copyPath);
            CHECK(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
            CHECK(a.readAll() == b.readAll());
            restored.openReport("", originalId);
            CHECK(restored.current()["_conflict_copy_id"] == copyId);
            restored.clearLocalCopies();
            CHECK(QFileInfo::exists(originalPath) && QFileInfo::exists(copyPath));
            restored.openReport("", copyId);
            restored.removePhoto(p["id"].toString());
            CHECK(QFileInfo::exists(originalPath));
        }
        {
            Lem restored(root);
            restored.demo();
            restored.openReport("", originalId);
            CHECK(restored.current()["_conflict"].toBool());
            CHECK(QFileInfo::exists(originalPath));
        }
    }
    if (!app.arguments().contains("--network")) {
        qInfo() << "PASS: edit journals/restart and protected draft/closed-photo cache policy";
        return 0;
    }
    Lem c(state.isEmpty() ? temp.path() + "/network" : state + "/device");
    bool sawTransfer = false, sawPhotoContext = false, sawWaiting = false, validProgress = true;
    QObject::connect(&c, &Lem::operationChanged, &app, [&] {
        const auto overall = c.operationStatus();
        const auto progress = overall["progress"].toDouble();
        validProgress &= progress >= 0 && progress <= 1;
        for (const auto &value : c.operationTransfers()) {
            const auto t = value.toMap();
            sawTransfer = true;
            sawWaiting |= t["waiting"].toBool();
            sawPhotoContext |= t["context"].toString().contains("SITE.02");
            validProgress &= t["progress"].toDouble() >= 0 && t["progress"].toDouble() <= 1;
        }
    });
    c.setServer("http://127.0.0.1:18765");
    c.login("admin@lem.pt", "IntegrationPassword123!");
    CHECK(settled(c));
    CHECK(!c.user().isEmpty());
    CHECK(c.authenticated());
    c.createContract("Offline performance", 2026, 1, "TRE");
    CHECK(settled(c));
    const auto contract = c.contracts().first().toMap()["id"].toString();
    c.selectContract(contract);
    CHECK(settled(c));
    c.analyseExcel(
        QUrl::fromLocalFile(app.arguments().value(app.arguments().indexOf("--xlsx") + 1)));
    CHECK(settled(c));
    c.previewExcel("Parque", 1,
                   {{"external_id", "Identificador"}, {"model", "Modelo"}, {"serial", "Série"}, {"connectors", "Tomadas"}});
    CHECK(settled(c));
    const auto importOperation = c.importPreview().value("operation_id").toString();
    CHECK(!QUuid(importOperation).isNull());
    c.commitExcel();
    CHECK(settled(c));
    if (args.contains("--retry-import")) {
        CHECK(c.importPreview().value("operation_id") == importOperation);
        c.commitExcel();
        CHECK(settled(c) && c.importPreview().isEmpty());
        qInfo() << "Retried the committed workbook with its original operation ID";
    }
    CHECK(!c.chargers().isEmpty());
    const auto charger = c.chargers().first().toMap()["id"].toString();
    if (args.contains("--delete-ack")) return exerciseDeleteAck(c, charger, source, state);
    c.openReport(charger);
    const auto frozenTests = c.current()["template"].toMap()["load_test_connectors"].toList();
    CHECK(frozenTests == QVariantList({"CCS2", "CCS2"}));
    bool foundFirst = false, foundSecond = false;
    for (const auto &section : c.sections())
        for (const auto &item : section.toMap()["items"].toList()) {
            if (item.toMap()["id"] == "CARG.01.01") foundFirst = item.toMap()["label"] == "Teste de carga do conector CCS2 #1";
            if (item.toMap()["id"] == "CARG.01.02") foundSecond = item.toMap()["label"] == "Teste de carga do conector CCS2 #2";
        }
    CHECK(foundFirst && foundSecond);
    const auto rid = c.current()["id"].toString();
    c.answer("SITE.02", "OK", "antes do envio");
    c.attach("SITE.02", QUrl::fromLocalFile(source));
    const auto first = c.current()["photos"].toList().first().toMap()["id"].toString();
    bool edited = false, removed = false, nonblocking = true;
    QString nextJob;
    c.sync();
    CHECK(c.syncing());
    CHECK(!c.busy());
    QTimer::singleShot(150, &app, [&] {
        nonblocking &= c.syncing() && !c.busy();
        c.answer("SITE.02", "NOK", "escrito durante envio");
        c.attach("SITE.02", QUrl::fromLocalFile(source));
        edited = true;
    });
    QTimer::singleShot(500, &app, [&] {
        nonblocking &= c.syncing() && !c.busy();
        c.removePhoto(first);
        removed = true;
        c.openReport(charger);
        nextJob = c.current()["id"].toString();
        c.answer("FIN.03", "", "NEXT_JOB");
    });
    CHECK(settled(c));
    CHECK(edited && removed && nonblocking);
    CHECK(c.current()["id"].toString() == nextJob && nextJob != rid);
    c.openReport("", rid);
    CHECK(!c.busy());
    CHECK(c.current()["dirty"].toBool());
    CHECK(c.current()["photos"].toList().size() == 1);
    CHECK(c.current()["deleted_photos"].toList().size() == 1);
    CHECK(c.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
          "escrito durante envio");
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    CHECK(c.current()["deleted_photos"].toList().isEmpty());
    Lem other(temp.path() + "/other");
    other.setServer("http://127.0.0.1:18765");
    other.login("admin@lem.pt", "IntegrationPassword123!");
    CHECK(settled(other));
    other.selectContract(contract);
    CHECK(settled(other));
    other.openReport("", rid);
    CHECK(settled(other));
    CHECK(other.current()["photos"].toList().size() == 1);
    auto remote = other.current()["photos"].toList().first().toMap();
    CHECK(remote["id"].toString() != first);
    CHECK(remote["path"].toString().isEmpty());
    CHECK(other.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
          "escrito durante envio");
    // Repeat cached opening without requests, then explicitly fetch the one photo.
    other.openReport("", rid);
    CHECK(!other.busy());
    const auto pid = remote["id"].toString();
    other.fetchPhoto(pid);
    CHECK(until([&] { return !other.photoDownloading(pid); }));
    CHECK(!other.current()["photos"].toList().first().toMap()["path"].toString().isEmpty());
    // Bulk download reuses the one cached image and pins the report for offline use.
    c.openReport("", rid);
    for (int i = 0; i < 3; ++i)
        c.attach("SITE.02", QUrl::fromLocalFile(source));
    c.sync();
    CHECK(settled(c));
    other.refreshReport();
    CHECK(settled(other));
    CHECK(other.photoDownloadSummary()["total"].toInt() == 4);
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 3);
    CHECK(other.photoDownloadSummary()["bytes"].toLongLong() > 0);
    other.downloadReportPhotos();
    CHECK(other.downloadingPhotos() && !other.busy());
    CHECK(other.currentReportDownloadingPhotos());
    other.deleteReport(rid); // Never delete the report whose files are in flight.
    CHECK(other.current()["id"].toString() == rid);
    other.openReport(charger);
    const auto visibleDuringDownload = other.current()["id"].toString();
    CHECK(visibleDuringDownload != rid);
    CHECK(!other.currentReportDownloadingPhotos());
    other.answer("SITE.02", "OK", "editar enquanto outro report descarrega");
    CHECK(other.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["value"] == "OK");
    other.deleteReport(visibleDuringDownload);
    CHECK(other.current().isEmpty() && other.downloadingPhotos());
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.current().isEmpty()); // Download callbacks must not reopen the deleted draft.
    other.openReport("", rid);
    CHECK(other.current()["offline_photos"].toBool());
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 0);
    other.downloadReportPhotos();
    CHECK(!other.downloadingPhotos());
    for (auto value : other.current()["photos"].toList())
        CHECK(QFile::remove(QUrl(value.toMap()["path"].toString()).toLocalFile()));
    other.downloadReportPhotos();
    other.cancelReportPhotoDownloads();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() <= 4);
    other.downloadReportPhotos();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 0);
    // The runner fails the ninth GET once; retry automatically and finish the batch.
    for (auto value : other.current()["photos"].toList())
        CHECK(QFile::remove(QUrl(value.toMap()["path"].toString()).toLocalFile()));
    other.downloadReportPhotos();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 0);
    CHECK(other.photoDownloadState().contains("Todas"));
    other.downloadReportPhotos();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 0);
    other.releaseReportPhotos();
    CHECK(!other.current()["offline_photos"].toBool());
    // A lost PUT acknowledgement must not turn newer local typing into a conflict.
    c.openReport(charger);
    const auto retryId = c.current()["id"].toString();
    c.answer("FIN.03", "", "LOSE_ACK");
    c.sync();
    QTimer::singleShot(100, &app, [&] { c.answer("FIN.03", "", "NEWER"); });
    CHECK(settled(c));
    CHECK(c.current()["dirty"].toBool());
    c.sync();
    CHECK(settled(c));
    if (c.current()["dirty"].toBool()) qCritical() << "Retry status:" << c.message() << c.syncState();
    CHECK(!c.current()["dirty"].toBool());
    CHECK(c.current()["payload"].toMap()["answers"].toMap()["FIN.03"].toMap()["notes"] == "NEWER");
    // No manual sync: the idle queue drains while the app remains open.
    c.answer("FIN.03", "", "AUTO");
    CHECK(until([&] { return !c.current()["dirty"].toBool() && !c.syncing(); }, 15000));
    c.sync();
    CHECK(!c.syncing());
    CHECK(settled(c)); // Manual synchronization now also receives catalogue/report changes.
    CHECK(c.current()["id"].toString() == retryId);
    // Complete while an older draft snapshot is in flight, then start another job.
    CHECK(completeAnswers(c, source));
    bool finishedDuringSync = false;
    bool signedDuringSync = false;
    QString following;
    c.sync();
    QTimer::singleShot(100, &app, [&] {
        // Replace a signature while an older draft/evidence is in flight. Its
        // acknowledgement must not erase the replacement or clear pending work.
        QFile png(source);
        signedDuringSync = c.syncing() && png.open(QIODevice::ReadOnly) &&
            c.saveSignature("FIN.02", "data:image/png;base64," + QString::fromLatin1(png.readAll().toBase64()));
        c.finish();
        finishedDuringSync = c.current()["status"].toString() == "closed" && !c.busy();
        c.openReport(charger);
        following = c.current()["id"].toString();
    });
    CHECK(settled(c));
    CHECK(signedDuringSync && finishedDuringSync && c.current()["id"].toString() == following);
    c.openReport("", retryId);
    CHECK(c.current()["status"].toString() == "closed" && c.current()["dirty"].toBool());
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    // Attachments stay local across idle/resume schedules until a group boundary.
    c.openReport(charger);
    const auto batchId = c.current()["id"].toString();
    c.attach("SITE.02", QUrl::fromLocalFile(source));
    CHECK(c.current()["_defer_photo_sync"].toBool());
    QElapsedTimer idle;
    idle.start();
    CHECK(until([&] { return idle.elapsed() > 9000; }, 10000));
    CHECK(c.current()["revision"].toInt() == 0 && !c.syncing());
    CHECK(!c.current()["photos"].toList()[0].toMap()["uploaded"].toBool());
    c.reportSyncBoundary();
    CHECK(until([&] { return c.syncing(); }, 4000));
    c.attach("SITE.02",
             QUrl::fromLocalFile(
                 source)); // New photograph during the older batch waits for the next boundary.
    CHECK(settled(c));
    CHECK(c.current()["id"].toString() == batchId && c.current()["dirty"].toBool());
    CHECK(c.current()["photos"].toList()[0].toMap()["uploaded"].toBool());
    CHECK(!c.current()["photos"].toList()[1].toMap()["uploaded"].toBool());
    idle.restart();
    CHECK(until([&] { return idle.elapsed() > 9000; }, 10000));
    CHECK(!c.current()["photos"].toList()[1].toMap()["uploaded"].toBool());
    c.reportSyncBoundary();
    CHECK(until([&] { return !c.current()["dirty"].toBool() && !c.syncing(); }, 6000));
    // Manual local cleanup preserves the server report and reopens without old image paths.
    c.openReport("", rid);
    CHECK(completeAnswers(c, source));
    c.finish();
    CHECK(settled(c));
    CHECK(c.current()["status"].toString() == "closed");
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    const int closedEvidenceCount = c.current()["photos"].toList().size();
    c.clearLocalCopies();
    CHECK(c.current()["id"].toString() == rid);
    c.openReport("", rid);
    CHECK(settled(c));
    CHECK(c.current()["id"].toString() == rid && c.current()["status"].toString() == "closed");
    CHECK(c.current()["photos"].toList().size() == closedEvidenceCount);
    for (auto value : c.current()["photos"].toList())
        CHECK(value.toMap()["path"].toString().isEmpty());
    c.openReport("", batchId);
    CHECK(settled(c)); // Await this client's refresh before testing deletion.
    // Remote removal is propagated to an already cached dirty copy via HTTP 410.
    other.openReport("", batchId);
    CHECK(settled(other));
    other.answer("SITE.02", "OK", "cópia antiga");
    c.deleteReport(batchId);
    CHECK(settled(c));
    CHECK(c.current().isEmpty());
    other.sync();
    CHECK(settled(other));
    CHECK(other.current().isEmpty());
    other.openReport("", batchId);
    CHECK(other.current().isEmpty());
    other.openReport("", retryId);
    CHECK(settled(other));
    c.openReport("", retryId);
    CHECK(settled(c));
    CHECK(c.current()["status"].toString() == "closed");
    c.deleteReport(retryId);
    CHECK(settled(c));
    CHECK(c.current().isEmpty());
    other.refresh();
    CHECK(settled(other));
    CHECK(other.current().isEmpty());
    other.openReport("", retryId);
    CHECK(other.current().isEmpty());
    // Ownership change while another device edits offline: preserve local work,
    // consult server separately, then copy all photographs with fresh identifiers.
    c.openReport(charger);
    c.answer("SITE.02", "OK", "servidor");
    c.attach("EST.01", QUrl::fromLocalFile(source));
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    const auto ownedId = c.current()["id"].toString();
    other.openReport("", ownedId);
    CHECK(settled(other));
    other.answer("SITE.02", "NOK", "alteração offline importante");
    other.attach("SITE.02", QUrl::fromLocalFile(source));
    c.createUser("ownership@example.test", "Novo proprietário", "OwnershipPassword123!");
    CHECK(settled(c));
    QString target;
    for (auto u : c.users())
        if (u.toMap()["email"] == "ownership@example.test")
            target = u.toMap()["id"].toString();
    CHECK(!target.isEmpty());
    c.assignUser(target);
    CHECK(settled(c));
    c.loadReportOwners();
    CHECK(settled(c));
    c.transferReport(target);
    CHECK(settled(c));
    CHECK(c.current()["owner_id"] == target);
    other.sync();
    CHECK(settled(other));
    CHECK(other.current()["_conflict"].toBool());
    const auto localPayload = other.current()["payload"];
    CHECK(other.current()["photos"].toList().size() == 2);
    other.viewServerReport();
    CHECK(settled(other));
    CHECK(other.current()["_conflict_remote"].toMap()["owner_id"] == target);
    CHECK(other.current()["payload"] == localPayload);
    other.copyLocalReport();
    CHECK(other.current()["id"] == ownedId);
    CHECK(other.message().contains("Descarregue"));
    other.downloadReportPhotos();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 0);
    other.copyLocalReport();
    CHECK(settled(other));
    const auto independent = other.current()["id"].toString();
    CHECK(independent != ownedId);
    CHECK(other.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
          "alteração offline importante");
    CHECK(other.current()["photos"].toList().size() == 2);
    other.answer("SITE.02", "OK", "cópia entretanto alterada");
    other.openReport("", ownedId);
    CHECK(other.current()["_conflict"].toBool());
    other.useServerReport();
    CHECK(!other.busy() && other.current()["_conflict"].toBool());
    CHECK(other.message().contains("outra cópia"));
    other.copyLocalReport();
    CHECK(settled(other));
    other.openReport("", ownedId);
    other.useServerReport();
    CHECK(settled(other));
    CHECK(!other.current()["dirty"].toBool() && other.current()["owner_id"] == target);
    CHECK(!other.current()["_conflict"].toBool());
    other.openReport("", independent);
    CHECK(other.current()["owner_id"] == other.user()["id"]);
    // A revision conflict between two devices belonging to the same owner also
    // offers a copy; ownership alone is insufficient to allow a stale overwrite.
    c.openReport(charger);
    c.answer("SITE.02", "OK", "base");
    c.sync();
    CHECK(settled(c));
    const auto revisionId = c.current()["id"].toString();
    other.openReport("", revisionId);
    CHECK(settled(other));
    other.answer("SITE.02", "NOK", "outra versão offline");
    c.answer("SITE.02", "OK", "versão mais recente");
    c.sync();
    CHECK(settled(c));
    other.sync();
    CHECK(settled(other));
    CHECK(other.current()["_conflict"].toBool());
    other.copyLocalReport();
    CHECK(settled(other));
    CHECK(other.current()["id"].toString() != revisionId);
    CHECK(other.current()["payload"].toMap()["source_report_id"] == revisionId);
    CHECK(sawTransfer && sawPhotoContext && validProgress);
    CHECK(c.operationTransfers().isEmpty());
    // A fully uploaded draft keeps answers/metadata locally while its cached
    // evidence bytes are removed; server evidence remains available.
    c.attach("EST.01", QUrl::fromLocalFile(source));
    c.reportSyncBoundary();
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    CHECK(c.current()["status"] == "draft");
    const auto draftPhotoPath = QUrl(c.current()["photos"].toList().first().toMap()["path"].toString()).toLocalFile();
    CHECK(QFileInfo::exists(draftPhotoPath));
    c.clearLocalCopies();
    CHECK(c.current()["id"].toString() == revisionId && !QFileInfo::exists(draftPhotoPath));
    c.openReport("", revisionId);
    CHECK(settled(c));
    CHECK(c.current()["id"].toString() == revisionId && c.current()["status"] == "draft");
    CHECK(c.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["value"] == "OK");
    CHECK(c.current()["photos"].toList().size() == 1);
    CHECK(c.current()["photos"].toList().first().toMap()["path"].toString().isEmpty());
    qInfo() << "PASS: synced draft metadata retained locally; cached photograph removed; server evidence preserved";
    c.answer("FIN.03", "", "LOSE_DELETE_ACK");
    c.sync();
    CHECK(settled(c) && !c.current()["dirty"].toBool());
    const auto removedPhoto = c.current()["photos"].toList().first().toMap()["id"].toString();
    c.removePhoto(removedPhoto);
    c.sync();
    CHECK(settled(c));
    CHECK(c.current()["dirty"].toBool() && !c.current()["_conflict"].toBool());
    const auto deleteCache = QDir::cleanPath(QFileInfo(draftPhotoPath).absolutePath() + "/../cache.json");
    const auto intent = read(deleteCache)["reports"].toObject()[revisionId].toObject();
    CHECK(intent["_inflight_delete_id"].toString() == removedPhoto);
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool() && !c.current()["_conflict"].toBool());
    CHECK(c.current()["photos"].toList().isEmpty());
    CHECK(c.current()["deleted_photos"].toList().isEmpty());
    CHECK(!c.current().contains("_inflight_delete_id"));
    qInfo() << "PASS: lost DELETE acknowledgement recovered with durable matching intent";

    c.openReport(charger);
    const auto a = c.current()["id"].toString();
    c.openReport(charger);
    const auto b = c.current()["id"].toString();
    const auto rejected = a < b ? a : b;
    const auto accepted = a < b ? b : a;
    c.openReport("", rejected);
    c.answer("FIN.03", "", "REJECT_413");
    c.openReport("", accepted);
    c.answer("FIN.03", "", "NEXT_AFTER_413");
    c.sync();
    CHECK(until([&] { return !c.current()["dirty"].toBool() && !c.syncing(); }, 12000));
    c.openReport("", rejected);
    CHECK(c.current()["dirty"].toBool());
    CHECK(c.current()["payload"].toMap()["answers"].toMap()["FIN.03"].toMap()["notes"] == "REJECT_413");
    qInfo() << "PASS: rejected report preserved; next queued report synchronized";
    // Two distinct users: a clean observer cache must follow the owner's closure.
    Lem owner(temp.path() + "/owner-visibility"), viewer(temp.path() + "/viewer-visibility");
    owner.setServer("http://127.0.0.1:18765");
    viewer.setServer("http://127.0.0.1:18765");
    owner.login("ownership@example.test", "OwnershipPassword123!"); CHECK(settled(owner));
    viewer.login("admin@lem.pt", "IntegrationPassword123!"); CHECK(settled(viewer));
    CHECK(owner.user()["id"] != viewer.user()["id"]);
    owner.selectContract(contract); CHECK(settled(owner));
    viewer.selectContract(contract); CHECK(settled(viewer));
    owner.openReport(charger);
    owner.answer("SITE.02", "OK", "rascunho do proprietário");
    owner.sync(); CHECK(settled(owner));
    CHECK(!owner.current()["dirty"].toBool());
    const auto sharedId = owner.current()["id"].toString();
    viewer.openReport(charger, sharedId); CHECK(settled(viewer));
    CHECK(viewer.current()["status"] == "draft");
    const auto oldRevision = viewer.current()["revision"].toInt();
    const auto readOnly = viewer.current()["payload"];
    viewer.answer("SITE.02", "NOK", "não pode editar");
    viewer.setAssessment("MAN.14", "NOK");
    viewer.attach("SITE.02", QUrl::fromLocalFile(source));
    viewer.finish();
    CHECK(viewer.current()["payload"] == readOnly);
    CHECK(!viewer.current()["dirty"].toBool() && viewer.current()["status"] == "draft");
    // Match the reported sequence exactly: the observer syncs the cached draft
    // before the technician closes it, without ever acquiring edit ownership.
    viewer.sync(); CHECK(settled(viewer));
    CHECK(viewer.current()["status"] == "draft" && !viewer.current()["dirty"].toBool());
    for (const auto &section : owner.sections())
        for (const auto &item : section.toMap()["items"].toList())
            owner.answer(item.toMap()["id"].toString(), "NA", "Sem conector/veículo disponível");
    owner.answer("FIN.03", "", "fecho confirmado pelo proprietário");
    owner.answer("EST.01", "OK", "");
    owner.attach("EST.01", QUrl::fromLocalFile(source)); // Required photograph to retain.
    owner.attach("SITE.02", QUrl::fromLocalFile(source)); // Optional photograph to remove.
    CHECK(addTestSignature(owner));
    owner.finish(); CHECK(settled(owner));
    CHECK(owner.current()["status"] == "closed" && !owner.current()["dirty"].toBool());
    const auto closedBeforeCorrection = QJsonObject::fromVariantMap(owner.current());
    QHash<QString, QByteArray> closedOriginals;
    for (const auto &value : closedBeforeCorrection["photos"].toArray()) {
        const auto photo = value.toObject();
        QFile original(QUrl(photo["path"].toString()).toLocalFile());
        CHECK(original.open(QIODevice::ReadOnly));
        closedOriginals.insert(photo["id"].toString(), original.readAll());
    }
    CHECK(closedOriginals.size() == 3);
    // Catalogue status must already be correct while the full cached report is still stale.
    bool catalogueClosedBeforeDownload = false;
    const auto catalogueWatch = QObject::connect(&viewer, &Lem::chargersChanged, [&] {
        if (viewer.current()["status"] != "draft") return;
        for (const auto &row : viewer.chargers())
            for (const auto &summary : row.toMap()["reports"].toList())
                if (summary.toMap()["id"] == sharedId && summary.toMap()["status"] == "closed")
                    catalogueClosedBeforeDownload = true;
    });
    viewer.sync(); CHECK(settled(viewer));
    QObject::disconnect(catalogueWatch);
    CHECK(catalogueClosedBeforeDownload);
    CHECK(viewer.current()["status"] == "closed");
    CHECK(viewer.current()["revision"].toInt() > oldRevision);
    CHECK(viewer.current()["payload"].toMap()["answers"].toMap()["FIN.03"].toMap()["notes"] == "fecho confirmado pelo proprietário");
    bool foundClosed = false;
    for (const auto &row : viewer.chargers())
        for (const auto &summary : row.toMap()["reports"].toList())
            if (summary.toMap()["id"] == sharedId) foundClosed = summary.toMap()["status"] == "closed";
    CHECK(foundClosed);
    viewer.openReport(charger, sharedId); CHECK(settled(viewer));
    CHECK(viewer.current()["status"] == "closed");
    // A legacy/corrupt dirty copy owned by somebody else must never be sent or discarded.
    LemReviewTest::staleForeignDraft(viewer);
    const auto protectedForeign = viewer.current()["payload"];
    // Contract presentation refreshes even a protected local copy, without
    // changing ownership, report revisions, answers or the local dirty flag.
    const auto closedRevision = owner.current()["revision"];
    const auto closedPayload = owner.current()["payload"];
    viewer.saveReportPresentation("CSMTEC", "APS", "Galp", 0); CHECK(settled(viewer));
    CHECK(viewer.current()["presentation"].toMap()["issuer"] == "CSMTEC");
    CHECK(viewer.current()["dirty"].toBool() && viewer.current()["payload"] == protectedForeign);
    viewer.openReport(charger);
    CHECK(viewer.current()["presentation"].toMap()["issuer"] == "CSMTEC");
    viewer.deleteReport(viewer.current()["id"].toString()); CHECK(settled(viewer));
    viewer.openReport(charger, sharedId); CHECK(settled(viewer));
    // A different device has not refreshed its catalogue. Export must still use
    // the live issuer, even when the settings writer's follow-up refresh failed.
    CHECK(owner.current()["presentation"].toMap()["issuer"] != "CSMTEC");
    Lem legacyAdmin(temp.path() + "/legacy-admin-export");
    legacyAdmin.setServer("http://127.0.0.1:18765");
    legacyAdmin.login("admin@lem.pt", "IntegrationPassword123!"); CHECK(settled(legacyAdmin));
    CHECK(legacyAdmin.user()["role"] == "admin");
    CHECK(owner.current()["owner_id"] != legacyAdmin.user()["id"]);
    LemReviewTest::cachedReport(legacyAdmin, QJsonObject::fromVariantMap(owner.current()));
    const auto brandingOutput = qEnvironmentVariable("LEM_BRANDING_TEST_OUTPUT", temp.path());
    for (auto exporter : {&owner, &legacyAdmin}) for (const auto &kind : {QString("pdf"), QString("bundle")}) {
        // A pre-branding server response had no contract ID or presentation. Its
        // charger catalogue may no longer be cached when opening the old report.
        LemReviewTest::legacyWithoutCatalogue(*exporter);
        const auto legacySnapshot = exporter->current()["snapshot"];
        const auto legacyPhotos = exporter->current()["photos"];
        const auto legacyOwner = exporter->current()["owner_id"];
        const auto destination = brandingOutput + (exporter == &owner ? "/legacy" : "/legacy-admin") +
                                 (kind == "pdf" ? ".pdf" : ".zip");
        exporter->exportReport(kind, QUrl::fromLocalFile(destination)); CHECK(settled(*exporter));
        CHECK(QFileInfo(destination).size() > 0);
        CHECK(exporter->current()["presentation"].toMap()["issuer"] == "CSMTEC");
        CHECK(exporter->current()["snapshot"] == legacySnapshot && exporter->current()["photos"] == legacyPhotos);
        CHECK(exporter->current()["owner_id"] == legacyOwner);
        CHECK(exporter->current()["revision"] == closedRevision && exporter->current()["payload"] == closedPayload);
        CHECK(!exporter->current()["dirty"].toBool());
    }
    // An unresolvable legacy report must preserve the destination instead of
    // successfully exporting the old LEM default after a server 404.
    auto unknownLegacy = QJsonObject::fromVariantMap(legacyAdmin.current());
    unknownLegacy["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    LemReviewTest::cachedReport(legacyAdmin, unknownLegacy);
    LemReviewTest::legacyWithoutCatalogue(legacyAdmin);
    const auto protectedDestination = temp.filePath("protected.pdf");
    QFile protectedFile(protectedDestination); CHECK(protectedFile.open(QIODevice::WriteOnly));
    CHECK(protectedFile.write("existing destination") > 0); protectedFile.close();
    legacyAdmin.exportReport("pdf", QUrl::fromLocalFile(protectedDestination)); CHECK(settled(legacyAdmin));
    CHECK(protectedFile.open(QIODevice::ReadOnly));
    CHECK(protectedFile.readAll() == "existing destination"); protectedFile.close();
    CHECK(legacyAdmin.message().contains("Não foi possível confirmar"));
    owner.refresh(); CHECK(settled(owner));
    CHECK(owner.current()["presentation"].toMap()["issuer"] == "CSMTEC");
    CHECK(owner.current()["revision"] == closedRevision && owner.current()["payload"] == closedPayload);
    CHECK(!owner.current()["dirty"].toBool());
    owner.saveReportPresentation("Lem", "", "", 1); // A technician cannot write settings.
    CHECK(!owner.busy() && owner.current()["presentation"].toMap()["issuer"] == "CSMTEC");
    viewer.saveReportPresentation("Lem", "Transdev", "", 1); CHECK(settled(viewer));
    owner.sync(); CHECK(settled(owner));
    CHECK(owner.current()["presentation"].toMap()["issuer"] == "Lem");
    CHECK(owner.current()["presentation"].toMap()["revision"].toInt() == 2);
    viewer.sync(); CHECK(settled(viewer));
    CHECK(viewer.current()["dirty"].toBool() && viewer.current()["payload"] == protectedForeign);
    CHECK(!viewer.current()["_conflict"].toBool()); // No forbidden upload was attempted.
    owner.refreshReport(); CHECK(settled(owner));
    CHECK(owner.current()["status"] == "closed");
    CHECK(owner.current()["payload"].toMap()["answers"].toMap()["FIN.03"].toMap()["notes"] == "fecho confirmado pelo proprietário");
    qInfo() << "PASS: two-user owner-only writes and closure; manual sync receives closed state in open report and catalogue";

    // A second device trashes a contract while this device has saved and unsent drafts.
    const auto retainedRoot = temp.path() + "/retention-owner";
    Lem retained(retainedRoot), administrator(temp.path() + "/retention-admin");
    administrator.setServer("http://127.0.0.1:18765");
    administrator.login("admin@lem.pt", "IntegrationPassword123!"); CHECK(settled(administrator));
    CHECK(administrator.authenticated());
    administrator.createContract("Retention integration", 2026, 1, "TRE"); CHECK(settled(administrator));
    QString retainedContract;
    for (const auto &entry : administrator.contracts())
        if (entry.toMap()["name"].toString().contains("Retention integration")) retainedContract=entry.toMap()["id"].toString();
    CHECK(!retainedContract.isEmpty());
    administrator.selectContract(retainedContract); CHECK(settled(administrator));
    administrator.analyseExcel(QUrl::fromLocalFile(app.arguments().value(app.arguments().indexOf("--xlsx") + 1))); CHECK(settled(administrator));
    administrator.previewExcel("Parque", 1, {{"external_id","Identificador"},{"model","Modelo"},{"serial","Série"},{"connectors","Tomadas"}}); CHECK(settled(administrator));
    administrator.commitExcel(); CHECK(settled(administrator));
    const auto retainedCharger=administrator.chargers().first().toMap()["id"].toString();
    administrator.assignUser(owner.user()["id"].toString()); CHECK(settled(administrator));
    retained.setServer("http://127.0.0.1:18765");
    retained.login("ownership@example.test", "OwnershipPassword123!"); CHECK(settled(retained));
    CHECK(retained.authenticated());
    retained.selectContract(retainedContract); CHECK(settled(retained));
    retained.openReport(retainedCharger);
    retained.attach("EST.01",QUrl::fromLocalFile(source));
    retained.sync(); CHECK(settled(retained));
    const auto retainedId=retained.current()["id"].toString();
    const auto retainedRevision=retained.current()["revision"];
    const auto retainedPhoto=QUrl(retained.current()["photos"].toList().first().toMap()["path"].toString()).toLocalFile();
    retained.answer("FIN.03","","PENDING_IN_TRASH");
    retained.openReport(retainedCharger);
    const auto neverSent=retained.current()["id"].toString();
    administrator.deleteContract(retainedContract,"APAGAR"); CHECK(settled(administrator));
    retained.sync(); CHECK(settled(retained));
    CHECK(retained.current().contains("_contract_unavailable") && retained.current()["dirty"].toBool());
    CHECK(retained.current()["revision"].toInt()==0);
    retained.openReport("",retainedId);
    CHECK(retained.current().contains("_contract_unavailable") && retained.current()["dirty"].toBool());
    CHECK(!retained.current()["_conflict"].toBool() && retained.current()["revision"]==retainedRevision);
    CHECK(QFileInfo::exists(retainedPhoto));
    retained.sync(); CHECK(settled(retained));
    CHECK(retained.current()["dirty"].toBool());
    // Suspension survives restart, and work in another contract can still be sent.
    Lem restarted(retainedRoot);
    restarted.openReport("",retainedId);
    CHECK(restarted.current().contains("_contract_unavailable"));
    CHECK(restarted.current()["payload"].toMap()["answers"].toMap()["FIN.03"].toMap()["notes"]=="PENDING_IN_TRASH");
    restarted.login("ownership@example.test","OwnershipPassword123!"); CHECK(settled(restarted));
    CHECK(restarted.authenticated());
    restarted.selectContract(contract); CHECK(settled(restarted));
    restarted.openReport(charger); restarted.answer("FIN.03","","OTHER_CONTRACT_CONTINUES");
    CHECK(!restarted.current().isEmpty() && restarted.current()["contract_id"].toString()==contract);
    restarted.sync(); CHECK(until([&] { return !restarted.current()["dirty"].toBool() && !restarted.syncing(); },15000));
    restarted.openReport("",retainedId); CHECK(restarted.current()["dirty"].toBool());
    administrator.restoreContract(retainedContract); CHECK(settled(administrator));
    restarted.refresh(); CHECK(restarted.refreshing());
    // The manual receive request must survive the already-running catalogue refresh.
    restarted.sync();
    const bool retentionResumed=until([&] { return !restarted.current().contains("_contract_unavailable") && !restarted.current()["dirty"].toBool() && !restarted.syncing(); },15000);
    if (!retentionResumed) qCritical()<<"Retention sync state:"<<restarted.message()<<restarted.syncState()<<restarted.current()["_contract_unavailable"]<<restarted.current()["_conflict_reason"]<<restarted.current()["_defer_photo_sync"];
    CHECK(retentionResumed);
    CHECK(restarted.current()["payload"].toMap()["answers"].toMap()["FIN.03"].toMap()["notes"]=="PENDING_IN_TRASH");
    CHECK(QFileInfo::exists(retainedPhoto));
    restarted.openReport("",neverSent); CHECK(!restarted.current()["dirty"].toBool());
    qInfo()<<"PASS: contract retention rejects sync; all drafts suspended durably; originals/answers preserved; other contracts continue; restoration resumes pending work";

    // Administrator correction of a technician's closed report. The server clones
    // evidence, so neither downloading originals nor transferring ownership is required.
    administrator.selectContract(contract); CHECK(settled(administrator));
    // A later intervention has already updated this station while the old report
    // still has its original frozen identity.
    administrator.openReport(charger);
    CHECK(completeAnswers(administrator, source));
    administrator.answer("EST.02", "Fabricante confirmado entretanto", "");
    administrator.answer("EST.03", "Modelo confirmado entretanto", "");
    administrator.answer("EST.04", "SN-CONFIRMADO", "");
    administrator.finish(); CHECK(settled(administrator));
    CHECK(administrator.current()["status"] == "closed" && !administrator.current()["dirty"].toBool());
    administrator.openReport(charger, sharedId); CHECK(settled(administrator));
    CHECK(administrator.current()["status"] == "closed");
    const auto originalOwner = administrator.current()["owner_id"];
    const auto originalIdentifier = administrator.current()["identifier"];
    const auto originalPhotoCount = administrator.current()["photos"].toList().size();
    CHECK(originalPhotoCount == 3);
    CHECK(administrator.photoDownloadSummary()["missing"].toInt() == originalPhotoCount);
    const auto originalPayload = LemReviewTest::persistedReport(administrator, sharedId).value("payload");
    CHECK(originalPayload.isObject());
    administrator.beginReportCorrection(); CHECK(settled(administrator));
    const auto correctionId = administrator.current()["id"].toString();
    CHECK(correctionId != sharedId && administrator.current()["status"] == "draft");
    CHECK(administrator.current()["correction"].toMap()["source_id"] == sharedId);
    CHECK(administrator.current()["photos"].toList().size() == originalPhotoCount);
    CHECK(administrator.photoDownloadSummary()["missing"].toInt() == originalPhotoCount);
    const auto inherited = administrator.current()["photos"].toList();
    QString removedId;
    for (const auto &entry : inherited) {
        const auto photo = entry.toMap();
        CHECK(photo["uploaded"].toBool() && photo["path"].toString().isEmpty());
        if (photo["checkpoint"] == "SITE.02") removedId = photo["id"].toString();
    }
    CHECK(!removedId.isEmpty());
    administrator.removePhoto(removedId);
    CHECK(administrator.current()["deleted_photos"].toList().contains(removedId));
    CHECK(administrator.current()["owner_id"] == administrator.user()["id"]);
    CHECK(!administrator.setCorrectionTimes("2026-11-16T12:00:00Z", "2026-11-16T10:00:00Z"));
    CHECK(!administrator.setCorrectionTimes("2026-11-16T09:00:00", "2026-11-16T10:00:00Z"));
    CHECK(administrator.setCorrectionTimes("2026-10-01T09:00:00+01:00", "2026-10-01T11:30:12+01:00"));
    administrator.answer("FIN.03", "", "LOSE_CORRECTION_ACK");
    administrator.answer("EST.02", "Fabricante corrigido pelo administrador", "");
    administrator.answer("EST.03", "Modelo corrigido pelo administrador", "");
    administrator.answer("EST.04", "SN-CORRECAO-ADMIN", "");
    administrator.attach("SITE.03", QUrl::fromLocalFile(source));
    const auto addedId = administrator.current()["photos"].toList().last().toMap()["id"].toString();
    CHECK(administrator.current()["photos"].toList().size() == originalPhotoCount);
    administrator.finish(); CHECK(settled(administrator));
    // The test API first returns the pre-fix 409. Manual Sync must revalidate
    // this saved, protected copy without deleting either local report.
    const auto blockedCorrection = LemReviewTest::persistedReport(administrator, correctionId);
    CHECK(blockedCorrection["dirty"].toBool() && blockedCorrection["_conflict"].toBool());
    CHECK(blockedCorrection["_conflict_reason"].toString().contains("Campo: model"));
    CHECK(LemReviewTest::persistedReport(administrator, sharedId)["payload"] == originalPayload);

    // A different contract can enter retention before the correction's retry.
    // Give its local draft a stable ID so the upload queue reaches that contract first.
    auto &contractOperator = legacyAdmin;
    CHECK(contractOperator.authenticated());
    contractOperator.deleteContract(retainedContract, "APAGAR"); CHECK(settled(contractOperator));
    const auto otherSample = LemReviewTest::persistedReport(restarted, neverSent);
    const QString otherPendingId = "00000000-0000-4000-8000-000000000001";
    CHECK(otherPendingId < correctionId && otherSample["contract_id"] == retainedContract);
    LemReviewTest::cachedReport(administrator, QJsonObject{
        {"id", otherPendingId}, {"charger_id", otherSample["charger_id"]},
        {"contract_id", retainedContract}, {"owner_id", administrator.user()["id"].toString()},
        {"revision", 0}, {"status", "draft"}, {"dirty", true},
        {"payload", otherSample["payload"]}, {"template", otherSample["template"]},
        {"snapshot", otherSample["snapshot"]}, {"photos", QJsonArray{}},
    });
    administrator.openReport("", correctionId);
    administrator.sync(); CHECK(settled(administrator));
    const auto retainedDraft = LemReviewTest::persistedReport(administrator, otherPendingId);
    CHECK(retainedDraft["_contract_unavailable"].toString().contains("Contrato apagado"));
    CHECK(retainedDraft["dirty"].toBool() && retainedDraft["payload"] == otherSample["payload"]);
    CHECK(administrator.current()["dirty"].toBool()); // Commit succeeded; its ACK was deliberately lost.
    CHECK(LemReviewTest::persistedReport(administrator, sharedId)["payload"] == originalPayload);
    // This separate device last exported a legacy report without a selected
    // contract. Open the replacement through its actual catalogue.
    contractOperator.selectContract(contract); CHECK(settled(contractOperator));
    CHECK(contractOperator.openReport(charger, correctionId) && settled(contractOperator));
    CHECK(contractOperator.current()["status"] == "closed");
    CHECK(!administrator.prepareExport("pdf"));
    qInfo() << "PASS: a different contract's HTTP 423 does not interrupt a manual correction retry";

    // The same correction can have both a saved model conflict and a suspension.
    // Restoration from another device must still allow lost-ACK recovery in one Sync.
    contractOperator.deleteContract(contract, "APAGAR"); CHECK(settled(contractOperator));
    administrator.viewServerReport(); CHECK(settled(administrator));
    CHECK(administrator.message().contains("Contrato apagado"));
    administrator.sync(); CHECK(settled(administrator));
    const auto suspendedCorrection = LemReviewTest::persistedReport(administrator, correctionId);
    CHECK(suspendedCorrection["_contract_unavailable"].toString().contains("Contrato apagado"));
    CHECK(suspendedCorrection["_conflict"].toBool() && suspendedCorrection["dirty"].toBool());
    CHECK(LemReviewTest::persistedReport(administrator, sharedId)["payload"] == originalPayload);
    contractOperator.restoreContract(contract); CHECK(settled(contractOperator));
    administrator.sync();
    const bool recovered = until([&] {
        return !administrator.current()["dirty"].toBool() && !administrator.syncing() && !administrator.refreshing();
    }, 12000);
    if (!recovered) qCritical() << administrator.message() << administrator.current()["_conflict_reason"];
    CHECK(recovered);
    CHECK(!administrator.current()["_conflict"].toBool());
    CHECK(!administrator.current().contains("_contract_unavailable"));
    CHECK(LemReviewTest::persistedReport(administrator, otherPendingId) == retainedDraft);
    CHECK(LemReviewTest::persistedReport(administrator, sharedId).isEmpty());
    CHECK(administrator.current()["correction"].toMap()["state"] == "committed");
    CHECK(administrator.current()["owner_id"] == originalOwner);
    CHECK(administrator.current()["photos"].toList().size() == originalPhotoCount);
    CHECK(administrator.current()["identifier"] == originalIdentifier);
    CHECK(administrator.current()["payload"].toMap()["started"] == "2026-10-01T08:00:00Z");
    CHECK(administrator.current()["payload"].toMap()["finished"] == "2026-10-01T10:30:12Z");
    // Commit acknowledgement was lost. Recovery must install the shared identity,
    // so the very next report is prefilled correctly without restarting the app.
    for (const auto &entry : administrator.chargers()) {
        const auto row = entry.toMap();
        if (row["id"].toString() != charger) continue;
        CHECK(row["details"].toMap()["manufacturer"] == "Fabricante corrigido pelo administrador");
        CHECK(row["details"].toMap()["model"] == "Modelo corrigido pelo administrador");
        CHECK(row["details"].toMap()["serial"] == "SN-CORRECAO-ADMIN");
    }
    // This computer still has the previous signed report and all three originals.
    // Open that same cached selection independently for the two export formats.
    for (const QString kind : {"pdf", "bundle"}) {
        auto cached = closedBeforeCorrection;
        QJsonArray originals;
        for (const auto &value : cached["photos"].toArray()) {
            auto photo = value.toObject();
            const auto id = photo["id"].toString();
            QFile file(temp.filePath(kind + "-" + id + ".png"));
            CHECK(file.open(QIODevice::WriteOnly) && file.write(closedOriginals.value(id)) > 0);
            file.close();
            photo["path"] = QUrl::fromLocalFile(file.fileName()).toString();
            originals.append(photo);
        }
        cached["photos"] = originals;
        CHECK(ReportPdf::validate(cached).isEmpty());
        LemReviewTest::cachedReport(owner, cached);
        QFile destination(temp.filePath(kind == "pdf" ? "previous.pdf" : "previous.zip"));
        CHECK(destination.open(QIODevice::WriteOnly) && destination.write("previous export") == 15);
        destination.close();
        owner.exportReport(kind, QUrl::fromLocalFile(destination.fileName())); CHECK(settled(owner));
        CHECK(destination.open(QIODevice::ReadOnly) && destination.readAll() == "previous export");
        CHECK(owner.current()["id"] == correctionId && notes(owner, "FIN.03") == "LOSE_CORRECTION_ACK");
    }
    qInfo() << "PASS: online PDF and ZIP from a complete second computer cache preserve the destination and open the accepted correction";
    owner.sync(); CHECK(settled(owner));
    owner.openReport(charger, correctionId); CHECK(settled(owner));
    CHECK(owner.current()["status"] == "closed" && owner.current()["owner_id"] == originalOwner);
    CHECK(notes(owner, "FIN.03") == "LOSE_CORRECTION_ACK");
    const auto finalPhotos = owner.current()["photos"].toList();
    QMap<QString, QVariantMap> confirmed;
    for (const auto &entry : finalPhotos)
        confirmed.insert(entry.toMap()["id"].toString(), entry.toMap());
    CHECK(confirmed.size() == originalPhotoCount && !confirmed.contains(removedId));
    CHECK(confirmed.value(addedId)["checkpoint"] == "SITE.03");
    for (const auto &entry : inherited) {
        const auto photo = entry.toMap();
        const auto id = photo["id"].toString();
        if (id != removedId) {
            CHECK(confirmed.contains(id) && !photo["digest"].toString().isEmpty());
            CHECK(confirmed.value(id)["digest"] == photo["digest"]);
            CHECK(confirmed.value(id)["checkpoint"] == photo["checkpoint"]);
        }
    }
    viewer.sync(); CHECK(settled(viewer));
    viewer.openReport(charger, sharedId); CHECK(settled(viewer));
    CHECK(viewer.current()["dirty"].toBool() && viewer.current()["_conflict"].toBool());
    CHECK(viewer.current()["_replacement_id"] == correctionId);
    CHECK(viewer.current()["payload"] == protectedForeign);
    administrator.beginReportCorrection(); CHECK(settled(administrator));
    const auto cancelledCorrection = administrator.current()["id"].toString();
    CHECK(cancelledCorrection != correctionId);
    administrator.deleteReport(cancelledCorrection); CHECK(settled(administrator));
    CHECK(administrator.current().isEmpty());
    administrator.openReport(charger, correctionId); CHECK(settled(administrator));
    administrator.beginReportCorrection(); CHECK(settled(administrator));
    CHECK(administrator.current()["id"].toString() != cancelledCorrection);
    CHECK(administrator.current()["correction"].toMap()["state"] == "editing");
    administrator.deleteReport(administrator.current()["id"].toString()); CHECK(settled(administrator));
    owner.refreshReport(); CHECK(settled(owner));
    CHECK(owner.current()["id"] == correctionId && owner.current()["status"] == "closed");
    qInfo()<<"PASS: desktop correction clones originals, recovers its lost commit ACK after contract restoration, restores technician ownership and preserves other devices' unsent work";

    qInfo()
        << "PASS: ownership transfer/offline revocation; revision conflict; protected server preview; independent photo/report copy; backup guards; operation stages/context/progress; local/remote/closed deletion and stale-copy cleanup; deferred group photo batches; bulk/cancel/retry/pin/photos; nonblocking edit/attach/remove; latest local changes survive ACK; lost ACK recovery; automatic idle sync; lazy photos and cached reopen";
    return 0;
}

#include "offline_sync.moc"
