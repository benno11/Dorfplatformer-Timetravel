# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# Keep SDL bridge classes and method names used by JNI_OnLoad registration.
-keep class org.libsdl.app.** { *; }

# Preserve all native method signatures so JNI lookups cannot break in release builds.
-keepclassmembers class * {
    native <methods>;
}

# Methods looked up explicitly via JNI GetStaticMethodID from native code.
-keepclassmembers class com.Benno111.dorfplatformertimetravel.MainActivity {
    public static java.lang.String httpGet(java.lang.String, int);
    public static java.lang.String firebaseSignIn(java.lang.String, java.lang.String, java.lang.String, int);
    public static java.lang.String firebaseLookupAccount(java.lang.String, java.lang.String, int);
    public static int firebaseUploadLevel(java.lang.String, java.lang.String, int);
    public static boolean showSoftKeyboard(int, int, int, int);
    public static void hideSoftKeyboard();
}
