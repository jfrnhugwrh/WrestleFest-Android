// Host stand-in for <android/log.h> (syntax checking only).
#ifndef WRESTLEFEST_HOST_STUB_ANDROID_LOG
#define WRESTLEFEST_HOST_STUB_ANDROID_LOG
enum { ANDROID_LOG_UNKNOWN = 0, ANDROID_LOG_DEFAULT, ANDROID_LOG_VERBOSE, ANDROID_LOG_DEBUG,
       ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR, ANDROID_LOG_FATAL, ANDROID_LOG_SILENT };
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
#endif
