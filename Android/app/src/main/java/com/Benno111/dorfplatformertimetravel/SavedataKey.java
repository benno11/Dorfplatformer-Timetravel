package com.Benno111.dorfplatformertimetravel;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import java.security.KeyStore;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Wraps the random save key with a non-exportable, per-install Android Keystore key. */
final class SavedataKey {
    private static final String ALIAS = "savedata_secret.v1";
    static synchronized byte[] transform(byte[] input, boolean encrypt) throws Exception {
        try {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (!store.containsAlias(ALIAS)) {
            if (!encrypt) throw new java.security.KeyStoreException("Save key unavailable");
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());
            generator.generateKey();
        }
        SecretKey key = (SecretKey) store.getKey(ALIAS, null);
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        if (encrypt) {
            cipher.init(Cipher.ENCRYPT_MODE, key);
            byte[] body = cipher.doFinal(input), iv = cipher.getIV();
            if (iv.length != 12) throw new java.security.GeneralSecurityException("Unexpected IV");
            byte[] result = Arrays.copyOf(iv, iv.length + body.length);
            System.arraycopy(body, 0, result, iv.length, body.length);
            return result;
        }
        if (input.length != 60) throw new java.security.GeneralSecurityException("Invalid wrapped key");
        cipher.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(128, input, 0, 12));
        return cipher.doFinal(input, 12, input.length - 12);
        } finally { Arrays.fill(input, (byte) 0); }
    }
}
