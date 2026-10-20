// Host stand-in for <jni.h> (syntax checking only).
#ifndef WRESTLEFEST_HOST_STUB_JNI
#define WRESTLEFEST_HOST_STUB_JNI
#include <cstdint>
struct _jobject;
typedef _jobject *jobject;
typedef _jobject *jclass;
typedef _jobject *jstring;
typedef _jobject *jmethodID;
typedef std::int32_t jint;
typedef jint jsize;
#define JNI_OK 0
#define JNI_ERR (-1)
struct JNIEnv {
    jclass GetObjectClass(jobject obj);
    jmethodID GetMethodID(jclass clazz, const char *name, const char *sig);
    jobject CallObjectMethod(jobject obj, jmethodID method, ...);
    const char *GetStringUTFChars(jstring str, bool *isCopy);
    void ReleaseStringUTFChars(jstring str, const char *chars);
    void DeleteLocalRef(jobject ref);
};
struct JavaVM {
    jint AttachCurrentThread(JNIEnv **env, void *args);
    jint DetachCurrentThread();
};
#endif
