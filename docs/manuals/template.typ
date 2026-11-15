// SPDX-License-Identifier: GPL-3.0-only
// Shared editable page design. Content is maintained in the two Markdown manuals.
#let manual(title: "", edition: "", audience: "", body) = {
  set document(title: title, author: "LEM")
  set text(font: "Arial", size: 10pt, lang: "pt", region: "PT")
  set page(paper: "a4", margin: (x: 20mm, y: 19mm),
    footer: context [#text(size: 8pt, fill: rgb("52616a"))[LEM · Manual de utilização] #h(1fr) #counter(page).display("1")])
  set par(justify: false, leading: 0.65em, spacing: 0.85em)
  set heading(numbering: none)
  show heading.where(level: 1): it => block(above: 8pt, below: 16pt)[#text(size: 22pt, weight: "bold", fill: rgb("007f91"))[#it.body]]
  show heading.where(level: 2): it => block(above: 12pt, below: 7pt)[#text(size: 12pt, weight: "bold")[#it.body]]
  set table(inset: 6pt, stroke: 0.4pt + rgb("d4e2e5"),
    fill: (x, y) => if y == 0 { rgb("e8f5f7") } else { none })
  image("lem-logo.png", width: 48mm)
  v(32mm)
  text(size: 32pt, weight: "bold", fill: rgb("007f91"))[#title]
  v(12mm)
  text(size: 11pt)[#edition]
  v(14mm)
  [Registar intervenções de manutenção, reunir evidências e entregar relatórios completos.]
  v(8mm)
  [*Para utilizadores finais*]
  if audience != "" {
    v(3mm)
    text(audience)
  }
  pagebreak()
  outline(title: [Índice], depth: 1)
  body
}

#let screenshot(path, caption) = figure(
  image(path, width: 100%, height: 110mm, fit: "contain"),
  caption: caption,
  supplement: [Figura],
)
