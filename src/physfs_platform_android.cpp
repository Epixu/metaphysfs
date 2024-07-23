/*
 * Android support routines for PhysicsFS.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "physfs_platforms.hpp"

#ifdef PHYSFS_PLATFORM_ANDROID

#include <jni.h>
#include <android/log.h>
#include "physfs_internal.h"

static char *prefpath = nullptr;


int __PHYSFS_platformInit(const char *argv0)
{
    return 1;  /* always succeed. */
} /* __PHYSFS_platformInit */


void __PHYSFS_platformDeinit(void)
{
    if (prefpath)
    {
        allocator.Free(prefpath);
        prefpath = nullptr;
    } /* if */
} /* __PHYSFS_platformDeinit */


void __PHYSFS_platformDetectAvailableCDs(PHYSFS_StringCallback cb, void *data)
{
    /* no-op. */
} /* __PHYSFS_platformDetectAvailableCDs */


char *__PHYSFS_platformCalcBaseDir(const char *argv0)
{
    /* as a cheat, we expect argv0 to be a PHYSFS_AndroidInit* on Android. */
    PHYSFS_AndroidInit *ainit = (PHYSFS_AndroidInit *) argv0;
    char *retval = nullptr;
    JNIEnv *jenv = nullptr;
    jobject jcontext;

    if (ainit == nullptr)
        return __PHYSFS_strdup("/");  /* oh well. */

    jenv = (JNIEnv *) ainit->jnienv;
    jcontext = (jobject) ainit->context;

    if ((*jenv)->PushLocalFrame(jenv, 16) >= 0)
    {
        jobject jfileobj = 0;
        jmethodID jmeth = 0;
        jthrowable jexception = 0;
        jstring jstr = 0;

        jmeth = (*jenv)->GetMethodID(jenv, (*jenv)->GetObjectClass(jenv, jcontext), "getPackageResourcePath", "()Ljava/lang/String;");
        jstr = (jstring)(*jenv)->CallObjectMethod(jenv, jcontext, jmeth);
        jexception = (*jenv)->ExceptionOccurred(jenv);  /* this can't throw an exception, right? Just in case. */
        if (jexception != nullptr)
            (*jenv)->ExceptionClear(jenv);
        else
        {
            const char *path = (*jenv)->GetStringUTFChars(jenv, jstr, nullptr);
            retval = __PHYSFS_strdup(path);
            (*jenv)->ReleaseStringUTFChars(jenv, jstr, path);
        } /* else */

        /* We only can rely on the Activity being valid during this function call,
           so go ahead and grab the prefpath too. */
        jmeth = (*jenv)->GetMethodID(jenv, (*jenv)->GetObjectClass(jenv, jcontext), "getFilesDir", "()Ljava/io/File;");
        jfileobj = (*jenv)->CallObjectMethod(jenv, jcontext, jmeth);
        if (jfileobj)
        {
            jmeth = (*jenv)->GetMethodID(jenv, (*jenv)->GetObjectClass(jenv, jfileobj), "getCanonicalPath", "()Ljava/lang/String;");
            jstr = (jstring)(*jenv)->CallObjectMethod(jenv, jfileobj, jmeth);
            jexception = (*jenv)->ExceptionOccurred(jenv);
            if (jexception != nullptr)
                (*jenv)->ExceptionClear(jenv);
            else
            {
                const char *path = (*jenv)->GetStringUTFChars(jenv, jstr, nullptr);
                const size_t len = strlen(path) + 2;
                prefpath = allocator.Malloc(len);
                if (prefpath)
                    snprintf(prefpath, len, "%s/", path);
                (*jenv)->ReleaseStringUTFChars(jenv, jstr, path);
            } /* else */
        } /* if */

        (*jenv)->PopLocalFrame(jenv, nullptr);
    } /* if */

    /* we can't return nullptr because then PhysicsFS will treat argv0 as a string, but it's a non-nullptr jobject! */
    if (retval == nullptr)
        retval = __PHYSFS_strdup("/");   /* we pray this works. */

    return retval;
} /* __PHYSFS_platformCalcBaseDir */


char *__PHYSFS_platformCalcPrefDir(const char *org, const char *app)
{
    return __PHYSFS_strdup(prefpath ? prefpath : "/");
} /* __PHYSFS_platformCalcPrefDir */

#endif /* PHYSFS_PLATFORM_ANDROID */

/* end of physfs_platform_android.c ... */

