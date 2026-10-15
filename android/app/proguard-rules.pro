# Keep the SDL/engine Java glue; it is referenced from native code and via
# reflection by SDLActivity.
-keep class org.libsdl.app.** { *; }
-keep class com.xray.cop.** { *; }
