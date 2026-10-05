/*
  libzippp.h -- exported declarations.
  Copyright (C) 2013 Cédric Tabin

  This file is part of libzippp, a library that wraps libzip for manipulating easily
  ZIP files in C++.
  The author can be contacted on http://www.astorm.ch/blog/index.php?contact

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.
  3. The names of the authors may not be used to endorse or promote
     products derived from this software without specific prior
     written permission.

  THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS
  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef WIN32
   // Disable compiler warning for strcpy
   #define _CRT_SECURE_NO_WARNINGS
#endif

#include <zip.h>
#include <errno.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>

#include "libzippp.h"

using namespace libzippp;
using namespace std;

// flags to apply when reading original entries
#define LIBZIPPP_ORIGINAL_STATE_FLAGS ZIP_FL_UNCHANGED | ZIP_FL_ENC_RAW

#define NEW_CHAR_ARRAY(nb) new (std::nothrow) char[(nb)];

/*
 * Feature detection of the underlying libzip: zip_encryption_method_supported exists
 * since libzip 1.2.0 and zip_compression_method_supported since libzip 1.7.0. They are
 * used to check, before anything is modified, that the compression/encryption method
 * selected on the archive can actually be applied. With an older libzip, the check
 * performed after the addition (with a full rollback of the call) is the only line of
 * defense.
 */
#if defined(LIBZIP_VERSION_MAJOR) && defined(LIBZIP_VERSION_MINOR)
  #if LIBZIP_VERSION_MAJOR>1 || (LIBZIP_VERSION_MAJOR==1 && LIBZIP_VERSION_MINOR>=2)
    #define LIBZIPPP_HAS_ENCRYPTION_METHOD_SUPPORTED 1
  #endif
  #if LIBZIP_VERSION_MAJOR>1 || (LIBZIP_VERSION_MAJOR==1 && LIBZIP_VERSION_MINOR>=7)
    #define LIBZIPPP_HAS_COMPRESSION_METHOD_SUPPORTED 1
  #endif
#endif

static libzippp_uint16 convertCompressionToLibzip(CompressionMethod comp) {
    switch(comp) {
        case CompressionMethod::STORE:
            return ZIP_CM_STORE;
#ifdef ZIP_CM_BZIP2
        case CompressionMethod::BZIP2:
            return ZIP_CM_BZIP2;
#endif
        case CompressionMethod::DEFLATE:
            return ZIP_CM_DEFLATE;
#ifdef ZIP_CM_XZ
        case CompressionMethod::XZ:
            return ZIP_CM_XZ;
#endif
#ifdef ZIP_CM_ZSTD
        case CompressionMethod::ZSTD:
            return ZIP_CM_ZSTD;
#endif
        default:
            return ZIP_CM_DEFAULT;
    }
}

static CompressionMethod convertCompressionFromLibzip(libzippp_uint16 comp) {
    switch(comp) {
      case ZIP_CM_STORE:
          return CompressionMethod::STORE;
#ifdef ZIP_CM_BZIP2
      case ZIP_CM_BZIP2:
          return CompressionMethod::BZIP2;
#endif
      case ZIP_CM_DEFLATE:
          return CompressionMethod::DEFLATE;
#ifdef ZIP_CM_XZ
      case ZIP_CM_XZ:
          return CompressionMethod::XZ;
#endif
#ifdef ZIP_CM_ZSTD
      case ZIP_CM_ZSTD:
          return CompressionMethod::ZSTD;
#endif
      default:
          return CompressionMethod::DEFAULT;
    }
}

/*
 * libzip accepts any compression level (it is simply stored, not validated): the
 * levels a method cannot use must be rejected here so that an invalid setting never
 * reaches the archive. A zero level always means "the default behavior of libzip";
 * for DEFLATE (and DEFAULT, which libzip maps to its default deflating behavior) the
 * supported non-zero levels are 1 to 9. Note that ZIP_CM_DEFAULT (-1) is stored in
 * an unsigned 16-bit value by libzippp, hence the cast.
 */
static bool isCompressionLevelApplicable(libzippp_uint16 comp, libzippp_uint32 level) {
    if (comp==ZIP_CM_DEFLATE || comp==(libzippp_uint16)ZIP_CM_DEFAULT) {
        return level<=9;
    }
    return true;
}

namespace Helper {
    static void callErrorHandlingCallbackFunc(const std::string& message, int zip_error_code, int system_error_code, ErrorHandlerCallback* callback) {
        zip_error_t error;
        zip_error_init(&error);
        zip_error_set(&error, zip_error_code, system_error_code);
        std::string strerror(zip_error_strerror(&error));
        (*callback)(message, strerror, zip_error_code, system_error_code);
        zip_error_fini(&error);
    }

    static void callErrorHandlingCallback(zip* zipHandle, const std::string& msg, ErrorHandlerCallback* callback) {
        if (zipHandle!=nullptr) {
            zip_error_t* error_code = zip_get_error(zipHandle);
            callErrorHandlingCallbackFunc(msg, error_code->zip_err, error_code->sys_err, callback);
        } else {
            callErrorHandlingCallbackFunc("", -1, -1, callback);
        }
    }

    static void callErrorHandlingCallback(zip_error_t* error, const std::string& msg, ErrorHandlerCallback* callback) {
        int error_code_zip, error_code_system;
        error_code_zip = zip_error_code_zip(error);
        error_code_system = zip_error_code_system(error);
        callErrorHandlingCallbackFunc(msg, error_code_zip, error_code_system, callback);
    }
}

static void defaultErrorHandler(const std::string& message,
                                const std::string& strerror,
                                int /*zip_error_code*/,
                                int /*system_error_code*/)
{
    fprintf(stderr, message.c_str(), strerror.c_str());
}

ZipEntry::ZipEntry(void) : zipFile(nullptr), index(0), time(0), compressionMethod(ZIP_CM_DEFAULT), compressionLevel(0), encryptionMethod(ZIP_EM_NONE), size(0), sizeComp(0), crc(0) {
}

string ZipEntry::getComment(void) const {
    //a null entry or an entry whose opening ended (possibly because the archive itself
    //was destroyed) must not dereference zipFile
    if (isNull()) { return string(); }
    if (session.expired()) { return string(); }
    return zipFile->getEntryComment(*this);
}

bool ZipEntry::setComment(const string& str) const {
    if (isNull()) { return false; }
    if (session.expired()) { return false; }
    return zipFile->setEntryComment(*this, str);
}

bool ZipEntry::setCompressionMethod(CompressionMethod compMethod) {
    if (isNull()) { return false; }
    if (session.expired()) { return false; }
    return zipFile->setEntryCompressionMethod(*this, compMethod);
}

CompressionMethod ZipEntry::getCompressionMethod(void) const {
    return convertCompressionFromLibzip(compressionMethod);
}

bool ZipEntry::setCompressionLevel(libzippp_uint32 level) {
    if (isNull()) { return false; }
    if (session.expired()) { return false; }
    return zipFile->setEntryCompressionLevel(*this, level);
}

string ZipEntry::readAsText(ZipArchive::State state, libzippp_uint64 size) const {
    if (isNull()) { return string(); }
    if (session.expired()) { return string(); } //the opening this entry comes from has ended
    char* content = (char*)zipFile->readEntry(*this, true, state, size);
    if (content==nullptr) { return string(); } //happen if the ZipArchive has been closed or the entry does not exist in the state

    //the read length is computed from the entry AS IT IS IN THE REQUESTED STATE instead of
    //from the size captured when this object was obtained: a replacement may be longer,
    //shorter or empty and the object is not required to be fetched again to read it
    libzippp_uint64 maxSize = zipFile->getEntry((libzippp_int64)index, state).getSize();
    libzippp_uint64 length = size==0 || size>maxSize ? maxSize : size;
    string str(content, length); //the extra '\0' appended by readEntry is not part of the content
    delete[] content;
    return str;
}

libzippp_uint8* ZipEntry::readAsBinary(ZipArchive::State state, libzippp_uint64 size) const {
    if (isNull()) { return nullptr; }
    if (session.expired()) { return nullptr; }
    return (libzippp_uint8*)zipFile->readEntry(*this, false, state, size);
}

basic_string<libzippp_uint8> ZipEntry::readAsBinaryString(ZipArchive::State state, libzippp_uint64 size) const {
    if (isNull()) { return basic_string<libzippp_uint8>(); }
    if (session.expired()) { return basic_string<libzippp_uint8>(); }
    libzippp_uint8* content = (libzippp_uint8*)zipFile->readEntry(*this, true, state, size);
    if (content==nullptr) { return basic_string<libzippp_uint8>(); } //happen if the ZipArchive has been closed or the entry does not exist in the state

    //see readAsText: the length must reflect the requested state (zero bytes inside the
    //content are preserved, the extra '\0' is only an allocation artifact)
    libzippp_uint64 maxSize = zipFile->getEntry((libzippp_int64)index, state).getSize();
    libzippp_uint64 length = size==0 || size>maxSize ? maxSize : size;
    basic_string<libzippp_uint8> str(content, length);
    delete[] content;
    return str;
}

int ZipEntry::readContent(std::ostream& ofOutput, ZipArchive::State state, libzippp_uint64 chunksize) const {
   //an invalid stream is reported first, as for a valid entry; the error codes below
   //are the ones guaranteed when the stream itself is valid
   if (!ofOutput) { return LIBZIPPP_ERROR_INVALID_PARAMETER; }
   //a null entry is invalid; a non-null entry whose opening ended is reported as coming
   //from a no-longer-open archive. The archive object itself may have been destroyed,
   //hence these checks must not dereference zipFile.
   if (isNull()) { return LIBZIPPP_ERROR_INVALID_ENTRY; }
   if (session.expired()) { return LIBZIPPP_ERROR_NOT_OPEN; }
   return zipFile->readEntry(*this, ofOutput, state, chunksize);
}

ZipArchive::ZipArchive(const string& zipPath, const string& password, Encryption encryptionMethod) : path(zipPath), zipHandle(nullptr), zipSource(nullptr), mode(NotOpen), password(password), progressPrecision(LIBZIPPP_DEFAULT_PROGRESSION_PRECISION), bufferData(nullptr), bufferLength(0), useArchiveCompressionMethod(false), compressionMethod(ZIP_CM_DEFAULT), compressionLevel(0), errorHandlingCallback(defaultErrorHandler) {
    switch(encryptionMethod) {
#ifdef LIBZIPPP_WITH_ENCRYPTION
        case Encryption::Aes128:
            this->encryptionMethod = ZIP_EM_AES_128;
            break;
        case Encryption::Aes192:
            this->encryptionMethod = ZIP_EM_AES_192;
            break;
        case Encryption::Aes256:
            this->encryptionMethod = ZIP_EM_AES_256;
            break;
        case Encryption::TradPkware:
            this->encryptionMethod = ZIP_EM_TRAD_PKWARE;
            break;
#endif
        case Encryption::None:
        default:
            this->encryptionMethod = ZIP_EM_NONE;
            break;
    }
}

void ZipArchive::beginEntrySession(void) {
    entrySession = std::make_shared<ZipEntrySession>();
}

void ZipArchive::endEntrySession(void) {
    //releasing the last strong reference expires every weak_ptr held by the entries
    //obtained during this opening, including all their copies
    entrySession.reset();
    //the compression settings tracked for the entries belong to the opening that
    //just ended: a later opening must not inherit them
    entryCompressionConfigs.clear();
}

bool ZipArchive::isEntryUsable(const ZipEntry& entry) const {
    if (entry.isNull()) { return false; }
    if (entry.zipFile!=this) { return false; }
    //the entry must belong to the CURRENT opening of this archive: its session marker
    //is the one this archive holds right now
    std::shared_ptr<ZipEntrySession> entryOpening = entry.session.lock();
    return entryOpening && entryOpening==entrySession;
}

ZipArchive::~ZipArchive(void) {
    close(); /* discard ??? */

    //if the commit failed in close(), the archive is gone anyway: the adopted buffers
    //must not outlive it (on a successful close/discard this is a no-op)
    releaseAdoptedBuffers();

    //whatever close() did (including a failed commit), the archive object itself is
    //gone: every entry obtained from any opening must be expired now
    endEntrySession();

    // ensures all the values are clared
    zipHandle = nullptr;
    zipSource = nullptr;
    bufferData = nullptr;
    errorHandlingCallback = nullptr;
    listeners.clear();
}

void ZipArchive::free(ZipArchive* archive) {
    delete archive;
}

ZipArchive* ZipArchive::fromBuffer(const void* data, libzippp_uint32 size, bool checkConsistency,
                                   const std::string& password, Encryption encryptionMethod) {
    void* mutableData = const_cast<void*>(data);
    ZipArchive* za = new ZipArchive("", password, encryptionMethod);
    bool o = za->openBuffer(&mutableData, size, ZipArchive::ReadOnly, checkConsistency);
    if (!o) {
        delete za;
        za = nullptr;
    }
    return za;
}

ZipArchive* ZipArchive::fromWritableBuffer(void** data, libzippp_uint32 size, OpenMode mode, bool checkConsistency,
                                           const std::string& password, Encryption encryptionMethod) {
    ZipArchive* za = new ZipArchive("", password, encryptionMethod);
    bool o = za->openBuffer(data, size, mode, checkConsistency);
    if (!o) {
        delete za;
        za = nullptr;
    }
    return za;
}

ZipArchive* ZipArchive::fromSource(zip_source* source, OpenMode om, bool checkConsistency,
                                   const std::string& password, Encryption encryptionMethod) {
    ZipArchive* za = new ZipArchive("", password, encryptionMethod);
    bool o = za->openSource(source, om, checkConsistency);
    if (!o) {
        delete za;
        za = nullptr;
    }
    return za;
}

bool ZipArchive::openBuffer(void** data, libzippp_uint32 size, OpenMode om, bool checkConsistency) {
    zip_error_t error;
    zip_error_init(&error);

    /* create source from buffer */
    zip_source* localZipSource = zip_source_buffer_create(*data, size, 0, &error);
    if (localZipSource == nullptr) {
        Helper::callErrorHandlingCallback(&error, "can't create zip source: %s\n", errorHandlingCallback);
        zip_error_fini(&error);
        return false;
    }

    bool open = openSource(localZipSource, om, checkConsistency);
    if (open) {
        if (om==Write || om==New) {
            bufferData = data;
            bufferLength = size;

            //prevents libzip to delete the source when closing the ZipArchive
            zip_source_keep(localZipSource);
        }
    } else {
        zip_source_free(localZipSource);
        localZipSource = nullptr;
    }
    return open;
}

bool ZipArchive::openSource(zip_source* source, OpenMode om, bool checkConsistency) {
    int zipFlag = 0;
    if (om == ReadOnly) { zipFlag = 0; }
    else if (om == Write) { zipFlag = ZIP_CREATE; }
    else if (om == New) { zipFlag = ZIP_CREATE | ZIP_TRUNCATE; }
    else { return false; }
    if (checkConsistency) {
        zipFlag = zipFlag | ZIP_CHECKCONS;
    }

    zip_error_t error;
    zip_error_init(&error);

    /* open zip archive from source */
    zipHandle = zip_open_from_source(source, zipFlag, &error);
    if (zipHandle == nullptr) {
        Helper::callErrorHandlingCallback(&error, "can't open zip from source: %s\n", errorHandlingCallback);
        zip_error_fini(&error);
        return false;
    }
    zip_error_fini(&error);

    zipSource = source;

#ifdef LIBZIPPP_WITH_ENCRYPTION
    if (isEncrypted()) {
        int result = zip_set_default_password(zipHandle, password.c_str());
        if (result != 0) {
            close();
            return false;
        }
    }
#endif

    mode = om;
    beginEntrySession(); //entries created by this opening share this marker
    return true;
}

bool ZipArchive::open(OpenMode om, bool checkConsistency) {
    if (isOpen()) { return om==mode; }

    int zipFlag = 0;
    if (om==ReadOnly) { zipFlag = 0; }
    else if (om==Write) { zipFlag = ZIP_CREATE; }
    else if (om==New) { zipFlag = ZIP_CREATE | ZIP_TRUNCATE; }
    else { return false; }

    if (checkConsistency) {
        zipFlag = zipFlag | ZIP_CHECKCONS;
    }

    int errorFlag = 0;
    zipHandle = zip_open(path.c_str(), zipFlag, &errorFlag);

    //error during opening of the file
    if (errorFlag!=ZIP_ER_OK) {
        zip_error_t error;
        zip_error_init_with_code(&error, errorFlag);
        Helper::callErrorHandlingCallback(&error, "unable to open archive: %s\n", errorHandlingCallback);
        zip_error_fini(&error);

        zipHandle = nullptr;
        return false;
    }

    if (zipHandle!=nullptr) {
#ifdef LIBZIPPP_WITH_ENCRYPTION
        if (isEncrypted()) {
            int result = zip_set_default_password(zipHandle, password.c_str());
            if (result!=0) {
                close();
                return false;
            }
        }
#endif

        mode = om;
        beginEntrySession(); //entries created by this opening share this marker
        return true;
    }

    return false;
}

void progress_callback(zip* /*archive*/, double progression, void* ud) {
    ZipArchive* za = static_cast<ZipArchive*>(ud);
    vector<ZipProgressListener*> listeners = za->getProgressListeners();
    for(vector<ZipProgressListener*>::const_iterator it=listeners.begin() ; it!=listeners.end() ; ++it) {
        ZipProgressListener* listener = *it;
        listener->progression(progression);
    }
}

int progress_cancel_callback(zip* /*archive*/, void* ud) {
    ZipArchive* za = static_cast<ZipArchive*>(ud);
    vector<ZipProgressListener*> listeners = za->getProgressListeners();
    for(vector<ZipProgressListener*>::const_iterator it=listeners.begin() ; it!=listeners.end() ; ++it) {
        ZipProgressListener* listener = *it;
        if (listener->cancel())
          return 1;
    }
    return 0;
}

int ZipArchive::close(void) {
    if (isOpen()) {

        //do not handle zipSource at all because it will be deleted by libzip
        //directly when not necessary anymore

        if (!listeners.empty()) {
            zip_register_progress_callback_with_state(zipHandle, progressPrecision, progress_callback, nullptr, this);
            zip_register_cancel_callback_with_state(zipHandle, progress_cancel_callback, nullptr, this);
        }

        //avoid to reset the progress when unzipping
        if (mode != ReadOnly) {
            progress_callback(zipHandle, 0, this); //enforce the first progression call to be zero
        }

        int result = zip_close(zipHandle);
        if (result!=0) {
            //typically a cancelled commit: the archive is still open and usable, the
            //session is left untouched so existing entries and their copies remain valid
            Helper::callErrorHandlingCallback(zipHandle, "unable to close archive: %s\n", errorHandlingCallback);
            return LIBZIPPP_ERROR_HANDLE_FAILURE;
        }

        zipHandle = nullptr;
        //the opening really ended (even if the buffer readback below fails afterwards):
        //all entries obtained during it must expire now
        endEntrySession();
        //the commit consumed the sources: the buffers adopted with freeData=true are
        //no longer referenced by libzip and are released exactly once, here
        releaseAdoptedBuffers();
        progress_callback(zipHandle, 1, this); //enforce the last progression call to be one

        //push back the changes in the buffer
        int res_code = LIBZIPPP_OK;
        if (bufferData!=nullptr && (mode==New || mode==Write)) {
            int srcOpen = zip_source_open(zipSource);
            if (srcOpen==0) {
                //IMPORTANT: the rewritten archive is read back into a *freshly allocated*
                //buffer. We must never read into nor realloc *bufferData here: that memory
                //is the storage backing the libzip source we are currently reading from.
                //Reusing it aliases the source and, depending on the libzip version (i.e.
                //whether it takes ownership of the buffer when writing), it makes
                //zip_source_free() release a buffer that realloc() already moved/freed,
                //which results in a double-free (see double_free_report / fromWritableBuffer).
                zip_int64_t increment = 1024;
                zip_int64_t capacity = bufferLength>0 ? static_cast<zip_int64_t>(bufferLength) : increment;
                char* outBuffer = static_cast<char*>(malloc(capacity * sizeof(char)));
                char* tempBuffer = outBuffer;
                zip_int64_t tempBufferSize = capacity;
                zip_int64_t totalRead = 0;
                zip_int64_t read = (outBuffer==nullptr) ? -1 : zip_source_read(zipSource, tempBuffer, tempBufferSize);
                while(read>0) {
                    totalRead += read;
                    if (totalRead>=capacity) {
                        zip_int64_t newCapacity = capacity + increment;
                        char* grownBuffer = static_cast<char*>(realloc(outBuffer, newCapacity * sizeof(char)));
                        if (grownBuffer==nullptr) {
                            Helper::callErrorHandlingCallback(zipHandle, "can't read back from source: unable to extend buffer\n", errorHandlingCallback);
                            ::free(outBuffer);
                            outBuffer = nullptr;
                            read = -1;
                            break;
                        }
                        outBuffer = grownBuffer;
                        capacity = newCapacity;
                    }
                    tempBuffer = outBuffer+totalRead;
                    tempBufferSize = capacity-totalRead;
                    read = zip_source_read(zipSource, tempBuffer, tempBufferSize);
                }

                zip_source_close(zipSource);

                if (outBuffer!=nullptr && read>=0) {
                    //freeing zipSource first guarantees libzip has released whatever it owns
                    //before we touch *bufferData. With freep=0 (see openBuffer) libzip never
                    //frees *bufferData itself, so reclaiming it here is safe and leak-free.
                    zip_source_free(zipSource);
                    zipSource = nullptr;

                    ::free(*bufferData);
                    *bufferData = outBuffer;
                    bufferLength = totalRead;
                } else {
                    Helper::callErrorHandlingCallback(zipHandle, "can't read back from source: changes were not pushed in the buffer\n", errorHandlingCallback);
                    res_code = LIBZIPPP_ERROR_HANDLE_FAILURE;
                    if (outBuffer!=nullptr) { ::free(outBuffer); }
                    zip_source_free(zipSource);
                    zipSource = nullptr;
                }
            } else {
                Helper::callErrorHandlingCallback((zip*)nullptr, "can't read back from source: changes were not pushed in the buffer\n", errorHandlingCallback);
                res_code = LIBZIPPP_ERROR_HANDLE_FAILURE;

                zip_source_free(zipSource);
                zipSource = nullptr;
            }
        }

        mode = NotOpen;
        return res_code;
    }

    return LIBZIPPP_OK;
}

void ZipArchive::discard(void) {
    if (isOpen()) {
        zip_discard(zipHandle);
        zipHandle = nullptr;

        //the pending sources are gone: release the buffers adopted with freeData=true
        releaseAdoptedBuffers();

        if (bufferData!=nullptr && (mode==New || mode==Write)) {
            zip_source_free(zipSource);
            zipSource = nullptr;
        }

        mode = NotOpen;
        endEntrySession(); //discarded opening: all its entries expire
    }
}

bool ZipArchive::unlink(void) {
    if (isOpen()) { discard(); }
    int result = remove(path.c_str());
    return result==0;
}

string ZipArchive::getComment(State state) const {
    if (!isOpen()) { return string(); }

    int flag = 0;
    if (state==Original) { flag = flag | LIBZIPPP_ORIGINAL_STATE_FLAGS; }
    else { flag = flag | ZIP_FL_ENC_GUESS; }

    int length = 0;
    const char* comment = zip_get_archive_comment(zipHandle, &length, flag);
    if (comment==nullptr) { return string(); }
    return string(comment, length);
}

bool ZipArchive::setComment(const string& comment) const {
    if (!isOpen()) { return false; }
    if (mode==ReadOnly) { return false; }

    string::size_type size = comment.size();
    const char* data = comment.c_str();
    int result = zip_set_archive_comment(zipHandle, data, (zip_uint16_t)size);
    return result==0;
}

bool ZipArchive::setEntryCompressionConfig(ZipEntry& entry, CompressionMethod comp, libzippp_uint32 level) const {
    if (!isOpen()) { return false; }
    if (!isEntryUsable(entry)) { return false; }
    if (mode==ReadOnly) { return false; }
    const libzippp_uint16 comp_libzip = convertCompressionToLibzip(comp);

    //libzip does not validate the compression level: reject here the levels the
    //selected method cannot use, so that neither the entry nor the archive is changed
    if (!isCompressionLevelApplicable(comp_libzip, level)) { return false; }

    //ZIP_CM_DEFAULT (-1) is stored in an unsigned 16-bit value: sign-extend it back
    //for libzip, which rejects the truncated 65535 as an unknown method
    bool success = zip_set_file_compression(zipHandle, entry.index, (zip_int32_t)(zip_int16_t)comp_libzip, level)==0;
    if (success) {
        entry.compressionMethod = comp_libzip;
        entry.compressionLevel = level;
        //tracked for the whole opening so that Current-state queries (including of
        //copies obtained later, e.g. after a rename) report these exact settings
        entryCompressionConfigs[entry.index] = EntryCompressionConfig(comp_libzip, level);
    }
    return success;
}

bool ZipArchive::setEntryCompressionMethod(ZipEntry& entry, CompressionMethod comp) const {
    if (!isOpen()) { return false; }
    if (!isEntryUsable(entry)) { return false; }

    //only the method changes: the level currently effective for the entry is
    //preserved. It may have been chosen through another copy of the entry after
    //this ZipEntry object was obtained, hence it is resolved from the settings
    //tracked by the archive (0 means the default behavior of libzip) and never
    //from the possibly stale value captured by this copy.
    libzippp_uint32 level = 0;
    std::map<libzippp_uint64, EntryCompressionConfig>::const_iterator it = entryCompressionConfigs.find(entry.index);
    if (it!=entryCompressionConfigs.end()) { level = it->second.level; }
    return setEntryCompressionConfig(entry, comp, level);
}

bool ZipArchive::setEntryCompressionLevel(ZipEntry& entry, libzippp_uint32 level) const {
    if (!isOpen()) { return false; }
    if (!isEntryUsable(entry)) { return false; }

    //only the level changes: the method currently effective for the entry is
    //preserved (see setEntryCompressionMethod)
    libzippp_uint16 method;
    std::map<libzippp_uint64, EntryCompressionConfig>::const_iterator it = entryCompressionConfigs.find(entry.index);
    if (it!=entryCompressionConfigs.end()) {
        method = it->second.method;
    } else {
        //no compression change is pending for this entry: its effective method is
        //the one of the current state of the archive
        struct zip_stat stat;
        zip_stat_init(&stat);
        if (zip_stat_index(zipHandle, entry.index, ZIP_FL_ENC_GUESS, &stat)!=0) { return false; }
        method = stat.comp_method;
    }
    return setEntryCompressionConfig(entry, convertCompressionFromLibzip(method), level);
}

libzippp_int64 ZipArchive::getNbEntries(State state) const {
    if (!isOpen()) { return LIBZIPPP_ERROR_NOT_OPEN; }

    int flag = state==Original ? LIBZIPPP_ORIGINAL_STATE_FLAGS : 0;
    return zip_get_num_entries(zipHandle, flag);
}

ZipEntry ZipArchive::createEntry(struct zip_stat* stat, State state) const {
    string name(stat->name);
    libzippp_uint64 index = stat->index;
    libzippp_uint64 size = stat->size;
    //the compression method of an entry is always its own: the default method
    //selected on the archive only applies to the entries added afterwards and must
    //never mask the method of an existing (or already staged) entry
    libzippp_uint16 compMethod = stat->comp_method;
    //libzip does not expose the compression level of an entry: it is only known for
    //the settings explicitly chosen during this opening (tracked by index). The
    //original state never borrows a level set afterwards.
    libzippp_uint32 compLevel = 0;
    if (state==Current) {
        std::map<libzippp_uint64, EntryCompressionConfig>::const_iterator it = entryCompressionConfigs.find(index);
        if (it!=entryCompressionConfigs.end()) {
            compMethod = it->second.method;
            compLevel = it->second.level;
        }
    }
    libzippp_uint16 encMethod = stat->encryption_method;
    libzippp_uint64 sizeComp = stat->comp_size;
    int crc = stat->crc;
    time_t time = stat->mtime;

    return ZipEntry(this, entrySession, name, index, time, compMethod, compLevel, encMethod, size, sizeComp, crc);
}

int ZipArchive::stateFlags(State state) {
    return state==Original ? LIBZIPPP_ORIGINAL_STATE_FLAGS : ZIP_FL_ENC_GUESS;
}

ZipEntry ZipArchive::resolveEntry(const ZipEntry& zipEntry, State state, libzippp_uint64* outSize) const {
    if (outSize!=nullptr) { *outSize = 0; }

    //a null-ZipEntry (e.g. obtained through getEntry in a state where the name does not
    //exist) cannot be resolved in any state, nor can an entry coming from another archive
    //or from an opening that has ended
    if (!isEntryUsable(zipEntry)) { return ZipEntry(); }

    int flag = stateFlags(state);
    struct zip_stat stat;
    zip_stat_init(&stat);

    //stat the (stable) slot in the requested state: this fails for an entry deleted in the
    //current state (read with Current) or a newly added entry (read with Original)
    if (zip_stat_index(zipHandle, zipEntry.getIndex(), flag, &stat)!=0) { return ZipEntry(); }

    ZipEntry resolved = createEntry(&stat, state);
    if (outSize!=nullptr) { *outSize = stat.size; }
    return resolved;
}

vector<ZipEntry> ZipArchive::getEntries(State state) const {
    if (!isOpen()) { return vector<ZipEntry>(); }

    struct zip_stat stat;
    zip_stat_init(&stat);

    vector<ZipEntry> entries;
    int flag = state==Original ? LIBZIPPP_ORIGINAL_STATE_FLAGS : ZIP_FL_ENC_GUESS;
    libzippp_int64 nbEntries = getNbEntries(state);
    for(libzippp_int64 i=0 ; i<nbEntries ; ++i) {
        int result = zip_stat_index(zipHandle, i, flag, &stat);
        if (result==0) {
            ZipEntry entry = createEntry(&stat, state);
            entries.push_back(entry);
        } else {
            //TODO handle read error => crash ?
        }
    }
    return entries;
}

bool ZipArchive::hasEntry(const string& name, bool excludeDirectories, bool caseSensitive, State state) const {
    if (!isOpen()) { return false; }

    int flags = 0;
    if (excludeDirectories) { flags = flags | ZIP_FL_NODIR; }
    if (!caseSensitive) { flags = flags | ZIP_FL_NOCASE; }
    if (state==Original) { flags = flags | LIBZIPPP_ORIGINAL_STATE_FLAGS; }
    else { flags = flags | ZIP_FL_ENC_GUESS; }

    libzippp_int64 index = zip_name_locate(zipHandle, name.c_str(), flags);
    return index>=0;
}

ZipEntry ZipArchive::getEntry(const string& name, bool excludeDirectories, bool caseSensitive, State state) const {
    if (isOpen()) {
        int flags = 0;
        if (excludeDirectories) { flags = flags | ZIP_FL_NODIR; }
        if (!caseSensitive) { flags = flags | ZIP_FL_NOCASE; }
        if (state==Original) { flags = flags | LIBZIPPP_ORIGINAL_STATE_FLAGS; }
        else { flags = flags | ZIP_FL_ENC_GUESS; }

        libzippp_int64 index = zip_name_locate(zipHandle, name.c_str(), flags);
        if (index>=0) {
            //the located index must be stat'd in the SAME state: locating a renamed entry
            //in the original state gives the original slot, whose name and size are the
            //original ones (using the current stat here would return the new name/length)
            return getEntry(index, state);
        } else {
            //name not found in the requested state
        }
    }
    return ZipEntry();
}

ZipEntry ZipArchive::getEntry(libzippp_int64 index, State state) const {
    if (isOpen()) {
        struct zip_stat stat;
        zip_stat_init(&stat);
        int flag = state==Original ? LIBZIPPP_ORIGINAL_STATE_FLAGS : ZIP_FL_ENC_GUESS;
        int result = zip_stat_index(zipHandle, index, flag, &stat);
        if (result==0) {
            return createEntry(&stat, state);
        } else {
            //index not found / invalid index
        }
    }
    return ZipEntry();
}

string ZipArchive::getEntryComment(const ZipEntry& entry, State state) const {
    if (!isOpen()) { return string(); }
    if (!isEntryUsable(entry)) { return string(); }

    int flag = 0;
    if (state==Original) { flag = flag | LIBZIPPP_ORIGINAL_STATE_FLAGS; }
    else { flag = ZIP_FL_ENC_GUESS; }

    unsigned int clen;
    const char* com = zip_file_get_comment(zipHandle, entry.getIndex(), &clen, flag);
    string comment = com==nullptr ? string() : string(com, clen);
    return comment;
}

bool ZipArchive::setEntryComment(const ZipEntry& entry, const string& comment) const {
    if (!isOpen()) { return false; }
    if (!isEntryUsable(entry)) { return false; }

    bool result = zip_file_set_comment(zipHandle, entry.getIndex(), comment.c_str(), (zip_uint16_t)comment.size(), ZIP_FL_ENC_GUESS) != 0;
    return result==0;
}

void* ZipArchive::readEntry(const ZipEntry& zipEntry, bool asText, State state, libzippp_uint64 size) const {
    if (!isOpen()) { return nullptr; }
    if (!isEntryUsable(zipEntry)) { return nullptr; }

    //resolve the entry in the requested state: the uncompressed size and the existence of
    //the entry depend on the state (a replacement may be longer, shorter or empty, a deleted
    //entry is only readable as Original and a newly added one only as Current)
    libzippp_uint64 entrySize = 0;
    ZipEntry resolved = resolveEntry(zipEntry, state, &entrySize);
    if (resolved.isNull()) { return nullptr; }

    int flag = stateFlags(state);
    struct zip_file* zipFile = zip_fopen_index(zipHandle, resolved.getIndex(), flag);
    if (zipFile) {
        //zero means "read the whole content" and a too big length returns all of it; the
        //captured zipEntry size is intentionally never used to bound the read
        libzippp_uint64 uisize = size==0 || size>entrySize ? entrySize : size;

        char* data = NEW_CHAR_ARRAY(uisize+(asText ? 1 : 0))
        if (!data) { //allocation error
            zip_fclose(zipFile);
            return nullptr;
        }

        libzippp_int64 result = zip_fread(zipFile, data, uisize);
        zip_fclose(zipFile);

        //avoid buffer copy
        if (asText) { data[uisize] = '\0'; }

        libzippp_int64 isize = (libzippp_int64)uisize;
        if (result==isize) {
            return data;
        } else { //unexpected number of bytes read => crash ?
            delete[] data;
        }
    } else {
        //unable to read the entry (e.g. it does not exist in the requested state) => crash ?
    }

    return nullptr;
}

void* ZipArchive::readEntry(const string& zipEntry, bool asText, State state, libzippp_uint64 size) const {
    //the name is interpreted in the requested state: there is no fallback to the other state
    //if the name does not exist in this one
    ZipEntry entry = getEntry(zipEntry, false, true, state);
    if (entry.isNull()) { return nullptr; }
    return readEntry(entry, asText, state, size);
}

int ZipArchive::deleteEntry(const ZipEntry& entry) const {
    if (!isOpen()) { return LIBZIPPP_ERROR_NOT_OPEN; }
    if (!isEntryUsable(entry)) { return LIBZIPPP_ERROR_INVALID_ENTRY; }
    if (mode==ReadOnly) { return LIBZIPPP_ERROR_NOT_ALLOWED; } //deletion not allowed

    if (entry.isFile()) {
        int result = zip_delete(zipHandle, entry.getIndex());
        if (result==0) { return 1; }
        return LIBZIPPP_ERROR_UNKNOWN; //unable to delete the entry
    } else {
        int counter = 0;
        vector<ZipEntry> allEntries = getEntries();
        vector<ZipEntry>::const_iterator eit;
        for(eit=allEntries.begin() ; eit!=allEntries.end() ; ++eit) {
            ZipEntry ze = *eit;
            string::size_type startPosition = ze.getName().find(entry.getName());
            if (startPosition==0) {
                int result = zip_delete(zipHandle, ze.getIndex());
                if (result==0) { ++counter; }
                else { return LIBZIPPP_ERROR_UNKNOWN; } //unable to remove the current entry
            }
        }
        return counter;
    }
}

int ZipArchive::deleteEntry(const string& e) const {
    ZipEntry entry = getEntry(e);
    if (entry.isNull()) { return LIBZIPPP_ERROR_INVALID_PARAMETER; }
    return deleteEntry(entry);
}

int ZipArchive::renameEntry(const ZipEntry& entry, const string& newNameIn) const {
    if (!isOpen()) { return LIBZIPPP_ERROR_NOT_OPEN; }
    if (!isEntryUsable(entry)) { return LIBZIPPP_ERROR_INVALID_ENTRY; }
    if (mode==ReadOnly) { return LIBZIPPP_ERROR_NOT_ALLOWED; } //renaming not allowed
    if (newNameIn.length()==0) { return LIBZIPPP_ERROR_INVALID_PARAMETER; }

    bool isDir = entry.isDirectory();
    string newName = newNameIn;
    if (isDir) {
        //a '/' is automatically appended to the destination of a directory
        if (!LIBZIPPP_ENTRY_IS_DIRECTORY(newName)) { newName += LIBZIPPP_ENTRY_PATH_SEPARATOR; }
    } else {
        if (LIBZIPPP_ENTRY_IS_DIRECTORY(newName)) { return LIBZIPPP_ERROR_INVALID_PARAMETER; } //a file cannot be renamed as a directory
    }

    string sourceName = entry.getName();
    if (newName==sourceName) { return LIBZIPPP_ERROR_INVALID_PARAMETER; }

    /*
     * Enumerate ALL the entries of the CURRENT state of the archive (i.e. including any
     * uncommitted addition/deletion/replacement/rename). Slots freed by zip_delete are
     * reported with a null name by libzip and simply skipped.
     */
    struct RenameItem {
        zip_uint64_t index;
        string oldName;
        string targetName;
    };
    vector<RenameItem> movedItems;
    vector<string> externalNames;

    zip_int64_t nbSlots = zip_get_num_entries(zipHandle, 0);
    for(zip_int64_t i=0 ; i<nbSlots ; ++i) {
        const char* currentName = zip_get_name(zipHandle, (zip_uint64_t)i, ZIP_FL_ENC_GUESS);
        if (currentName==nullptr) { continue; } //entry deleted in the current (uncommitted) state

        string name(currentName);
        bool participates = false;
        string targetName;
        if (isDir) {
            if (name==sourceName) {
                participates = true;
                targetName = newName;
            } else if (name.compare(0, sourceName.length(), sourceName)==0) {
                participates = true; //descendant of the moved directory
                targetName = newName + name.substr(sourceName.length());
            }
        } else {
            participates = (name==sourceName);
            targetName = newName;
        }

        if (participates) {
            movedItems.push_back(RenameItem{(zip_uint64_t)i, name, targetName});
        } else {
            externalNames.push_back(name);
        }
    }

    //the source entry itself must be part of the current archive
    bool sourceFound = false;
    for(vector<RenameItem>::const_iterator it=movedItems.begin() ; it!=movedItems.end() ; ++it) {
        if (it->oldName==sourceName) { sourceFound = true; break; }
    }
    if (!sourceFound) { return LIBZIPPP_ERROR_INVALID_ENTRY; }

    /*
     * Detect conflicts against the CURRENT state only: any entry that does not participate
     * in the move must not occupy the destination itself nor lie below it. Entries whose
     * parent directories are only implicit (no explicit directory entry in the archive) are
     * listed here as well, so a file such as new/target/last.txt prevents src/ from being
     * moved to new/target/ even when no new/ or new/target/ directory entry exists.
     */
    for(vector<string>::const_iterator it=externalNames.begin() ; it!=externalNames.end() ; ++it) {
        const string& external = *it;
        if (isDir) {
            if (external==newName || external.compare(0, newName.length(), newName)==0) {
                return LIBZIPPP_ERROR_UNKNOWN; //destination already occupied by an external entry: no overwrite, no merge
            }
        } else {
            if (external==newName) {
                return LIBZIPPP_ERROR_UNKNOWN; //the destination file already exists
            }
        }
    }

    /*
     * Compute the (explicit) parent directories that will be missing once the entries have
     * been moved. A directory that is itself moved never has to be re-created here (it keeps
     * existing as an entry, only its name changes), even when a moved entry's new parent
     * chain goes through it.
     */
    vector<string> parentsToCreate;
    for(vector<RenameItem>::const_iterator it=movedItems.begin() ; it!=movedItems.end() ; ++it) {
        //for a directory target (ending with '/'), the first parent to consider is above the
        //target directory itself, hence the search starts at the penultimate separator
        string::size_type searchPos = it->targetName.length()-1;
        if (LIBZIPPP_ENTRY_IS_DIRECTORY(it->targetName) && searchPos>0) { searchPos -= 1; }
        string::size_type slash = it->targetName.rfind(LIBZIPPP_ENTRY_PATH_SEPARATOR, searchPos);
        while(slash!=string::npos && slash>0) {
            string parent = it->targetName.substr(0, slash+1);

            //a moved destination already exists as an entry after the move
            bool isMovedDestination = false;
            for(vector<RenameItem>::const_iterator mit=movedItems.begin() ; mit!=movedItems.end() ; ++mit) {
                if (mit->targetName==parent) { isMovedDestination = true; break; }
            }
            if (isMovedDestination) { break; } //all the ancestors above are shared with this moved directory

            //an external entry already provides the directory (explicitly)
            bool isExternal = false;
            for(vector<string>::const_iterator eit=externalNames.begin() ; eit!=externalNames.end() ; ++eit) {
                if (*eit==parent) { isExternal = true; break; }
            }
            if (isExternal) { break; } //the prefix chain up to this directory already exists

            bool alreadyScheduled = false;
            for(vector<string>::const_iterator pit=parentsToCreate.begin() ; pit!=parentsToCreate.end() ; ++pit) {
                if (*pit==parent) { alreadyScheduled = true; break; }
            }
            if (!alreadyScheduled) { parentsToCreate.push_back(parent); }
            slash = it->targetName.rfind(LIBZIPPP_ENTRY_PATH_SEPARATOR, slash-1);
        }
    }
    //create the parents from the shallowest to the deepest
    std::sort(parentsToCreate.begin(), parentsToCreate.end());

    /*
     * Apply the rename deepest-first. This ordering is what makes moving a directory below
     * its own path possible (e.g. a/ to a/b/): the descendants free the names the ancestors
     * are renamed to, and the freshly re-created parent directories are not touched.
     */
    std::sort(movedItems.begin(), movedItems.end(),
              [](const RenameItem& a, const RenameItem& b) { return a.targetName > b.targetName; });

    vector<zip_uint64_t> renamedIndices;
    vector<string> renamedOldNames;
    bool failure = false;
    for(vector<RenameItem>::const_iterator it=movedItems.begin() ; it!=movedItems.end() ; ++it) {
        int result = zip_file_rename(zipHandle, it->index, it->targetName.c_str(), ZIP_FL_ENC_GUESS);
        if (result!=0) { failure = true; break; }
        renamedIndices.push_back(it->index);
        renamedOldNames.push_back(it->oldName);
    }

    //create the missing parent directories (e.g. re-create a/ when a/ moved to a/b/)
    vector<zip_uint64_t> createdDirIndices;
    if (!failure) {
        for(vector<string>::const_iterator it=parentsToCreate.begin() ; it!=parentsToCreate.end() ; ++it) {
            zip_int64_t dirIndex = zip_dir_add(zipHandle, it->c_str(), ZIP_FL_ENC_GUESS);
            if (dirIndex<0) { failure = true; break; }
            createdDirIndices.push_back((zip_uint64_t)dirIndex);
        }
    }

    if (failure) {
        /*
         * The move could not be completed: undo everything done during THIS call so that the
         * archive is restored to its exact pre-call state. The directories created during
         * the move are removed first (they may occupy the original names), then the renames
         * are replayed in the reverse order (shallowest-first). Any other pending change is
         * left untouched.
         */
        for(vector<zip_uint64_t>::reverse_iterator dit=createdDirIndices.rbegin() ; dit!=createdDirIndices.rend() ; ++dit) {
            zip_delete(zipHandle, *dit);
        }
        for(zip_int64_t k=(zip_int64_t)renamedIndices.size()-1 ; k>=0 ; --k) {
            zip_file_rename(zipHandle, renamedIndices[(size_t)k], renamedOldNames[(size_t)k].c_str(), ZIP_FL_ENC_GUESS);
        }
        return LIBZIPPP_ERROR_UNKNOWN;
    }

    return (int)movedItems.size();
}

int ZipArchive::renameEntry(const string& e, const string& newName) const {
    ZipEntry entry = getEntry(e);
    if (entry.isNull()) { return LIBZIPPP_ERROR_INVALID_PARAMETER; }
    return renameEntry(entry, newName);
}

void ZipArchive::releaseAdoptedBuffers(void) {
    for(vector<void*>::iterator it=adoptedBuffers.begin() ; it!=adoptedBuffers.end() ; ++it) {
        ::free(*it);
    }
    adoptedBuffers.clear();
}

bool ZipArchive::isArchiveCompressionApplicable(void) const {
    if (!useArchiveCompressionMethod && compressionLevel==0) { return true; }
    //libzip does not validate the compression level: an out-of-range level for the
    //selected method must be rejected here, before anything is changed
    if (!isCompressionLevelApplicable(compressionMethod, compressionLevel)) { return false; }
#ifdef LIBZIPPP_HAS_COMPRESSION_METHOD_SUPPORTED
    //ZIP_CM_DEFAULT (-1) is stored in an unsigned 16-bit member: sign-extend it back
    return zip_compression_method_supported((zip_int32_t)(zip_int16_t)compressionMethod, 1)!=0;
#else
    //cannot be checked beforehand: a failure is rolled back after the addition
    return true;
#endif
}

bool ZipArchive::isArchiveEncryptionApplicable(void) const {
#ifdef LIBZIPPP_WITH_ENCRYPTION
    if (isEncrypted()) {
  #ifdef LIBZIPPP_HAS_ENCRYPTION_METHOD_SUPPORTED
        return zip_encryption_method_supported((zip_uint16_t)encryptionMethod, 1)!=0;
  #else
        //cannot be checked beforehand: a failure is rolled back after the addition
        return true;
  #endif
    }
#endif
    return true;
}

bool ZipArchive::createParentDirectories(const string& entryName, vector<libzippp_uint64>& createdDirIndices) const {
    string::size_type lastSlash = entryName.rfind(LIBZIPPP_ENTRY_PATH_SEPARATOR);
    if (lastSlash==string::npos) { return true; } //no parent directory needed

    string::size_type nextSlash = entryName.find(LIBZIPPP_ENTRY_PATH_SEPARATOR);
    while (nextSlash!=string::npos && nextSlash<=lastSlash) {
        string pathToCreate = entryName.substr(0, nextSlash+1);
        if (!hasEntry(pathToCreate)) {
            libzippp_int64 dirIndex = zip_dir_add(zipHandle, pathToCreate.c_str(), ZIP_FL_ENC_GUESS);
            if (dirIndex<0) {
                //a failed addition must not leave empty parent directories behind
                rollbackCreatedDirectories(createdDirIndices);
                return false;
            }
            createdDirIndices.push_back((libzippp_uint64)dirIndex);
        }
        nextSlash = entryName.find(LIBZIPPP_ENTRY_PATH_SEPARATOR, nextSlash+1);
    }
    return true;
}

void ZipArchive::rollbackCreatedDirectories(const vector<libzippp_uint64>& createdDirIndices) const {
    //deepest directory first; only the directories created by the failed call are
    //removed, pre-existing ones are never touched
    for(vector<libzippp_uint64>::const_reverse_iterator it=createdDirIndices.rbegin() ; it!=createdDirIndices.rend() ; ++it) {
        zip_delete(zipHandle, *it);
    }
}

bool ZipArchive::installPreparedSource(const string& entryName, zip_source* source, const vector<libzippp_uint64>& createdDirIndices) const {
    libzippp_int64 result = zip_file_add(zipHandle, entryName.c_str(), source, ZIP_FL_OVERWRITE);
    if (result<0) {
        //the source was not adopted by libzip: release it without touching its data
        zip_source_free(source);
        rollbackCreatedDirectories(createdDirIndices);
        return false;
    }

    zip_file_set_mtime(zipHandle, result, time(nullptr), 0);

    bool settingsApplied = true;
    if (useArchiveCompressionMethod || compressionLevel!=0) {
        //the archive defaults are part of the actual write: both the method and the
        //level are applied (a zero level keeps the default behavior of libzip). The
        //int16 round-trip sign-extends ZIP_CM_DEFAULT (-1) back from its unsigned
        //16-bit storage, which libzip would reject as an unknown method.
        settingsApplied = zip_set_file_compression(zipHandle, result, (zip_int32_t)(zip_int16_t)compressionMethod, compressionLevel)==0;
        if (settingsApplied) {
            //tracked so that Current-state queries of the new entry report the
            //settings it will be written with (libzip does not expose the level)
            entryCompressionConfigs[(libzippp_uint64)result] = EntryCompressionConfig(compressionMethod, compressionLevel);
        }
    }
#ifdef LIBZIPPP_WITH_ENCRYPTION
    if (settingsApplied && isEncrypted()) {
        settingsApplied = zip_file_set_encryption(zipHandle, result, encryptionMethod, nullptr)==0;
    }
#endif

    if (!settingsApplied) {
        /*
         * Last-resort rollback: the applicability of the selected compression and
         * encryption methods was checked before anything was changed, so this is only
         * reachable on an unexpected libzip failure. The entry is reverted and the
         * directories created by this call are removed; every other pending change
         * (other entries, deletions, renames, comments) is left untouched. Reverting
         * the entry also frees the source (which never owns the caller's data here).
         */
        zip_unchange(zipHandle, (zip_uint64_t)result);
        rollbackCreatedDirectories(createdDirIndices);
        return false;
    }
    return true;
}

bool ZipArchive::addFile(const string& entryName, const string& file) const {
    if (!isOpen()) { return false; }
    if (mode==ReadOnly) { return false; } //adding not allowed
    if (LIBZIPPP_ENTRY_IS_DIRECTORY(entryName)) { return false; }

    //the source file must be readable: a missing file must not leave any trace
    //(neither a new entry nor parent directories) in the archive
    FILE* probe = fopen(file.c_str(), "rb");
    if (probe==nullptr) { return false; }
    fclose(probe);

    //the compression and encryption methods selected on the archive must be applicable
    //by this libzip: this is checked before anything is changed so that a failure
    //neither leaves a new entry behind nor silently falls back to weaker settings
    if (!isArchiveCompressionApplicable()) { return false; }
    if (!isArchiveEncryptionApplicable()) { return false; }

    vector<libzippp_uint64> createdDirIndices;
    if (!createParentDirectories(entryName, createdDirIndices)) { return false; }

    zip_source* source = zip_source_file(zipHandle, file.c_str(), 0, -1);
    if (source==nullptr) {
        //unable to create the zip_source
        rollbackCreatedDirectories(createdDirIndices);
        return false;
    }
    return installPreparedSource(entryName, source, createdDirIndices);
}

bool ZipArchive::addData(const string& entryName, const void* data, libzippp_uint64 length, bool freeData) const {
    if (!isOpen()) { return false; }
    if (mode==ReadOnly) { return false; } //adding not allowed
    if (LIBZIPPP_ENTRY_IS_DIRECTORY(entryName)) { return false; }

    //a null buffer is only valid for an empty content; anything else is rejected
    //before the archive is touched (the caller keeps the ownership of the data)
    if (data==nullptr && length>0) { return false; }

    //see addFile: the selected compression/encryption must be applicable beforehand
    if (!isArchiveCompressionApplicable()) { return false; }
    if (!isArchiveEncryptionApplicable()) { return false; }

    vector<libzippp_uint64> createdDirIndices;
    if (!createParentDirectories(entryName, createdDirIndices)) { return false; }

    /*
     * The buffer is always handed to libzip with freep=0, even when freeData is true:
     * the ownership is transferred to the archive (see adoptedBuffers) only once the
     * data has been fully accepted. Hence a failed call never frees the caller's
     * memory and a successful one guarantees the buffer is released exactly once,
     * when the pending changes are committed or discarded.
     */
    zip_source* source = zip_source_buffer(zipHandle, data, length, 0);
    if (source==nullptr) {
        //unable to create the zip_source
        rollbackCreatedDirectories(createdDirIndices);
        return false;
    }
    if (!installPreparedSource(entryName, source, createdDirIndices)) { return false; }

    if (freeData && data!=nullptr) {
        adoptedBuffers.push_back(const_cast<void*>(data));
    }
    return true;
}

bool ZipArchive::addData(const std::string& entryName, const std::basic_string<libzippp_uint8> data) const {
    /*
     * The argument is a copy that is destroyed when this method returns: its content
     * must be copied again into a buffer owned by the archive (through the freeData
     * contract) so that the staged source never references the dead argument when the
     * changes are committed or read back.
     */
    if (data.empty()) { return addData(entryName, nullptr, 0, false); }
    void* copy = malloc(data.size());
    if (copy==nullptr) { return false; }
    memcpy(copy, data.data(), data.size());
    if (!addData(entryName, copy, data.size(), true)) {
        //the ownership was not transferred: the copy is still ours to release
        ::free(copy);
        return false;
    }
    return true;
}

bool ZipArchive::addEntry(const string& entryName) const {
    if (!isOpen()) { return false; }
    if (mode==ReadOnly) { return false; } //adding not allowed
    if (!LIBZIPPP_ENTRY_IS_DIRECTORY(entryName)) { return false; }

    string::size_type nextSlash = entryName.find(LIBZIPPP_ENTRY_PATH_SEPARATOR);
    while (nextSlash!=string::npos) {
        string pathToCreate = entryName.substr(0, nextSlash+1);
        if (!hasEntry(pathToCreate)) {
            libzippp_int64 result = zip_dir_add(zipHandle, pathToCreate.c_str(), ZIP_FL_ENC_GUESS);
            if (result==-1) { return false; }
        }
        nextSlash = entryName.find(LIBZIPPP_ENTRY_PATH_SEPARATOR, nextSlash+1);
    }

    return true;
}

void ZipArchive::removeProgressListener(ZipProgressListener* listener) {
    for(vector<ZipProgressListener*>::const_iterator it=listeners.begin() ; it!=listeners.end() ; ++it) {
        ZipProgressListener* l = *it;
        if (l==listener) {
            listeners.erase(it);
            break;
        }
    }
}

void ZipArchive::setCompressionMethod(CompressionMethod comp)
{
    useArchiveCompressionMethod = comp!=CompressionMethod::DEFAULT;
    compressionMethod = convertCompressionToLibzip(comp);
}

CompressionMethod ZipArchive::getCompressionMethod(void) const {
    return convertCompressionFromLibzip(compressionMethod);
}

int ZipArchive::readEntry(const ZipEntry& zipEntry, std::ostream& ofOutput, State state, libzippp_uint64 chunksize) const {
    if (!ofOutput) { return LIBZIPPP_ERROR_INVALID_PARAMETER; }
    std::function<bool(const void*,libzippp_uint64)> writeFunc = [&ofOutput](const void* data,libzippp_uint64 size){ ofOutput.write((char*)data, size); return bool(ofOutput); };
    return readEntry(zipEntry, writeFunc, state, chunksize);
}

int ZipArchive::readEntry(const ZipEntry& zipEntry, std::function<bool(const void*,libzippp_uint64)> writeFunc, State state, libzippp_uint64 chunksize) const {
    if (!isOpen()) { return LIBZIPPP_ERROR_NOT_OPEN; }
    //null entries, entries from other archives and entries from a previous opening are
    //rejected before any data delivery (the output stream/callback is left untouched)
    if (!isEntryUsable(zipEntry)) { return LIBZIPPP_ERROR_INVALID_ENTRY; }

    //resolve the entry in the requested state: its existence and uncompressed size depend
    //on the state. When it does not exist in the state, no data is delivered at all.
    libzippp_uint64 maxSize = 0;
    ZipEntry resolved = resolveEntry(zipEntry, state, &maxSize);
    if (resolved.isNull()) { return LIBZIPPP_ERROR_FOPEN_FAILURE; }

    if (!chunksize) { chunksize = LIBZIPPP_DEFAULT_CHUNK_SIZE; } // use the default chunk size (512K) if not specified by the user

    int flag = stateFlags(state);
    struct zip_file* zipFile = zip_fopen_index(zipHandle, resolved.getIndex(), flag);
    if (!zipFile) { return LIBZIPPP_ERROR_FOPEN_FAILURE; }

    int iRes = LIBZIPPP_OK;

    //an empty content succeeds without reading anything; a single zero-length delivery is
    //still signalled to the output (this matches the historical behavior)
    if (maxSize==0) {
        if (!writeFunc("", 0)) {
            zip_fclose(zipFile);
            return LIBZIPPP_ERROR_OWRITE_FAILURE;
        }
        zip_fclose(zipFile);
        return LIBZIPPP_OK;
    }

    //only allocate what a single delivery may need (the whole content when it is smaller
    //than the chunk size) instead of the chunk size in every case
    libzippp_uint64 bufferSize = maxSize<chunksize ? maxSize : chunksize;
    char* data = NEW_CHAR_ARRAY(bufferSize)
    if (data!=nullptr) {
        //stream the exact, state-resolved length in successive chunks: this delivers the
        //whole content exactly once (chunksize bigger than, equal to, or not a divisor of
        //the content length are all handled by the same loop)
        libzippp_uint64 remaining = maxSize;
        while (remaining>0) {
            libzippp_uint64 toRead = remaining<bufferSize ? remaining : bufferSize;
            libzippp_int64 result = zip_fread(zipFile, data, toRead);
            if (result<0) {
                iRes = LIBZIPPP_ERROR_FREAD_FAILURE;
                break;
            }
            if (static_cast<libzippp_uint64>(result)!=toRead) {
                iRes = LIBZIPPP_ERROR_OWRITE_INDEX_FAILURE;
                break;
            }
            if (!writeFunc(data, toRead)) {
                iRes = LIBZIPPP_ERROR_OWRITE_FAILURE;
                break;
            }
            remaining -= toRead;
        }
        delete[] data;
    } else {
        iRes = LIBZIPPP_ERROR_MEMORY_ALLOCATION;
    }
    zip_fclose(zipFile);
    return iRes;
}
