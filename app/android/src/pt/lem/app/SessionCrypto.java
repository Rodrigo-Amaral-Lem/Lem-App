// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

package pt.lem.app;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import java.nio.ByteBuffer;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Protect the app's saved session with a non-exportable key from Android Keystore. */
public final class SessionCrypto {
    private static final String KEYSTORE = "AndroidKeyStore";
    private static final String ALIAS = "pt.lem.app.session.v1";
    private static final String TRANSFORMATION = "AES/GCM/NoPadding";
    private static final int IV_SIZE = 12;
    private static final int TAG_BITS = 128;

    private SessionCrypto() {}

    private static SecretKey key(boolean create) throws GeneralSecurityException, java.io.IOException {
        KeyStore store = KeyStore.getInstance(KEYSTORE);
        store.load(null);
        if (store.containsAlias(ALIAS))
            return (SecretKey) store.getKey(ALIAS, null);
        if (!create)
            return null;
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE);
        generator.init(new KeyGenParameterSpec.Builder(ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build());
        return generator.generateKey();
    }

    // The cipher supplies a fresh random IV. Prefix it to the authenticated ciphertext.
    public static byte[] seal(byte[] record) {
        try {
            Cipher cipher = Cipher.getInstance(TRANSFORMATION);
            cipher.init(Cipher.ENCRYPT_MODE, key(true));
            byte[] iv = cipher.getIV();
            if (iv.length != IV_SIZE)
                return null;
            byte[] encrypted = cipher.doFinal(record);
            return ByteBuffer.allocate(iv.length + encrypted.length).put(iv).put(encrypted).array();
        } catch (GeneralSecurityException | java.io.IOException | RuntimeException error) {
            return null;
        }
    }

    public static byte[] unseal(byte[] record) {
        if (record.length <= IV_SIZE + TAG_BITS / 8)
            return null;
        try {
            SecretKey key = key(false);
            if (key == null)
                return null;
            Cipher cipher = Cipher.getInstance(TRANSFORMATION);
            cipher.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(TAG_BITS, record, 0, IV_SIZE));
            return cipher.doFinal(record, IV_SIZE, record.length - IV_SIZE);
        } catch (GeneralSecurityException | java.io.IOException | RuntimeException error) {
            return null;
        }
    }
}
