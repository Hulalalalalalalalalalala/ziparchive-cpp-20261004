
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

#include <assert.h>
#include <string.h>
#include <iostream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "libzippp.h"
#include <zip.h>

using namespace std;
using namespace libzippp;

// Adds an entry directly through libzip. Unlike ZipArchive::addData, this helper does not
// create the parent directories, so files can be inserted with only implicit parent
// directories (no explicit "dir/" entry in the archive). A name ending with '/' creates a
// directory entry.
static void addRawEntry(zip* zh, const string& name, const char* content) {
    if (!name.empty() && name[name.size()-1]=='/') {
        assert(zip_dir_add(zh, name.c_str(), 0) >= 0);
    } else {
        zip_source_t* source = zip_source_buffer(zh, content, strlen(content), 0);
        assert(source != nullptr);
        assert(zip_file_add(zh, name.c_str(), source, 0) >= 0);
    }
}

class SimpleProgressListener : public ZipProgressListener {
public:
    SimpleProgressListener(void) : firstValue(-1), lastValue(-1) {}
    virtual ~SimpleProgressListener(void) {}

    double firstValue;
    double lastValue;

    void progression(double p) {
        cout << "-- Progression: " << p << endl;
        if(firstValue<0) { firstValue = p; }
        lastValue = p;
    }
    int cancel() {
      return 0;
    }
};

void test1() {
    cout << "Running test 1...";
    
    ZipArchive z1("test.zip");
    assert(!z1.isOpen());
    assert(!z1.isMutable());
    z1.open(ZipArchive::Write);
    assert(z1.isOpen());
    assert(z1.isMutable());
    bool result = z1.addEntry("folder/subfolder/finalfolder/");
    assert(result);
    assert(z1.close() == LIBZIPPP_OK);
    assert(!z1.isOpen());
    assert(!z1.isMutable());
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    assert(z2.isOpen());
    assert(!z2.isMutable());
    int nbEntries = z2.getNbEntries();
    assert(nbEntries==3);
    assert(z2.hasEntry("folder/"));
    assert(z2.hasEntry("folder/subfolder/"));
    assert(z2.hasEntry("folder/subfolder/finalfolder/"));
    assert(z2.close() == LIBZIPPP_OK);
    assert(z2.unlink());
    
    cout << " done." << endl;
}

void test2() {
    cout << "Running test 2...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    assert(DEFAULT == z1.getCompressionMethod());
    
    z1.open(ZipArchive::Write);
    z1.addData("somedata", txtFile, len);
    
    ZipEntry z1e1 = z1.getEntry("somedata");
    bool setcm = z1e1.setCompressionMethod(DEFLATE);
    assert(setcm);
    assert(DEFLATE == z1e1.getCompressionMethod());
    
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    assert(z2.getNbEntries()==1);
    assert(z2.hasEntry("somedata"));
    
    ZipEntry entry = z2.getEntry("somedata");
    assert(!entry.isNull());
    assert(entry.getCompressionMethod() == DEFLATE);
    
    string data = entry.readAsText();
    int clen = data.size();
    assert(clen==len);
    assert(strncmp(txtFile, data.c_str(), len)==0);
    
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test3() {
    cout << "Running test 3...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    basic_string<libzippp_uint8> basicStr((libzippp_uint8*)"012345");
    
    ZipArchive z1("test.zip");
    z1.setCompressionMethod(DEFLATE);
    assert(DEFLATE == z1.getCompressionMethod());
    
    z1.open(ZipArchive::Write);
    z1.addData("somedata/in/subfolder/data.txt", txtFile, len);
    z1.addData("somedata/basic_str", basicStr);
    
    //break the reading of basic_str in Travis CI
    /*assert(z1.addEntry("somedata/"));
    assert(z1.addEntry("in/"));
    assert(z1.addEntry("in/subfolder/"));*/
    
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    assert(z2.getNbEntries()==5);
    assert(z2.hasEntry("somedata/in/subfolder/data.txt"));
    assert(z2.hasEntry("somedata/basic_str"));
    
    ZipEntry entry = z2.getEntry("somedata/in/subfolder/data.txt");
    assert(!entry.isNull());
    assert(entry.getCompressionMethod() != DEFAULT);
    
    string data = entry.readAsText();
    int clen = data.size();
    assert(clen==len);
    assert(strncmp(txtFile, data.c_str(), len)==0);
    
    libzippp_uint8* rawData = entry.readAsBinary();
    assert(rawData!=nullptr);
    delete[] rawData;
    
    basic_string<libzippp_uint8> bstr = entry.readAsBinaryString();
    assert(data.size()==bstr.size());
    
    ZipEntry entryBasic = z2.getEntry("somedata/basic_str");
    basic_string<libzippp_uint8> bstr2 = entryBasic.readAsBinaryString();
    assert(basicStr.compare(bstr2)==0);
    
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test4() {
    cout << "Running test 4...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata/test.txt", txtFile, len);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    assert(z2.getNbEntries()==2);
    
    ZipEntry d = z2.getEntry("somedata/test.txt");
    assert(!d.isNull() && d.isFile());
    assert(z2.deleteEntry(d)==1);
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test5() {
    cout << "Running test 5...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata/in/subfolders/test.txt", txtFile, len);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    assert(z2.getNbEntries()==4);
    
    ZipEntry d = z2.getEntry("somedata/in/");
    assert(!d.isNull() && d.isDirectory());
    assert(z2.deleteEntry(d)==3);
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test6() {
    cout << "Running test 6...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata/in/subfolders/test.txt", txtFile, len);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    assert(z2.getNbEntries()==4);
    
    ZipEntry d = z2.getEntry("somedata/in/");
    assert(!d.isNull() && d.isDirectory());
    assert(z2.renameEntry(d, "somedata/out/")==3);
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test7() {
    cout << "Running test 7...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata/in/subfolders/test.txt", txtFile, len);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    assert(z2.getNbEntries()==4);
    
    ZipEntry d = z2.getEntry("somedata/in/");
    assert(!d.isNull() && d.isDirectory());
    assert(z2.renameEntry(d, "somedata/in/subfolder/")==3);
    z2.close();
    
    ZipArchive z3("test.zip");
    z3.open(ZipArchive::ReadOnly);
    assert(z3.getNbEntries()==5);
    z3.close();
    z3.unlink();
    
    cout << " done." << endl;
}

void test8() {
    cout << "Running test 8...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata/in/subfolders/test.txt", txtFile, len);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    assert(z2.getNbEntries()==4);
    
    ZipEntry d = z2.getEntry("somedata/in/");
    assert(!d.isNull() && d.isDirectory());
    assert(z2.renameEntry(d, "newdata/out/subfolders/")==3);
    z2.close();
    
    ZipArchive z3("test.zip");
    z3.open(ZipArchive::ReadOnly);
    assert(z3.getNbEntries()==6);
    z3.close();
    z3.unlink();
    
    cout << " done." << endl;
}

void test9() {
    cout << "Running test 9...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata/in/subfolders/test.txt", txtFile, len);
    z1.addData("somedata/out/subfolders/other.txt", txtFile, len);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    assert(z2.getNbEntries()==7);
    
    ZipEntry d = z2.getEntry("somedata/in/");
    assert(!d.isNull() && d.isDirectory());
    assert(z2.renameEntry(d, "root/")==3);
    z2.close();
    
    ZipArchive z3("test.zip");
    z3.open(ZipArchive::ReadOnly);
    assert(z3.getNbEntries()==7);
    z3.close();
    z3.unlink();
    
    cout << " done." << endl;
}

void test10() {
    cout << "Running test 10...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata/in/subfolders/test.txt", txtFile, len);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    assert(z2.getNbEntries()==4);
    
    ZipEntry d = z2.getEntry("somedata/in/");
    assert(!d.isNull() && d.isDirectory());
    assert(z2.renameEntry(d, "newdata/out/subfolders/")==3);
    z2.discard();
    
    ZipArchive z3("test.zip");
    z3.open(ZipArchive::ReadOnly);
    assert(z3.getNbEntries()==4);
    z3.close();
    z3.unlink();
    
    cout << " done." << endl;
}

void test11() {
    cout << "Running test 11...";
    
    string c = "a basic comment";
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addEntry("test/");
    z1.setComment(c);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    string str = z2.getComment();
    assert(str == c);
    z2.close();
    
    ZipArchive z3("test.zip");
    z3.open(ZipArchive::Write);
    z3.removeComment();
    z3.close();
    
    ZipArchive z4("test.zip");
    z4.open(ZipArchive::ReadOnly);
    string str2 = z4.getComment();
    assert(str2.empty());
    z4.close();
    z4.unlink();
    
    cout << " done." << endl;
}

void test12() {
    cout << "Running test 12...";
    
    string c = "a basic comment";
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addEntry("test/");
    z1.addData("file/data.txt", c.c_str(), c.length());
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    z2.addEntry("content/new/");
    z2.addData("newfile.txt", c.c_str(), c.length());
    assert(z2.getNbEntries(ZipArchive::Current)==6);
    assert(z2.getNbEntries(ZipArchive::Original)==3);
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test13() {
    cout << "Running test 13...";
    
    string c = "some example of text";
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addEntry("test/");
    z1.addData("file/data.txt", c.c_str(), c.length());
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    z2.renameEntry(z2.getEntry("file/data.txt"), "file/subdir/data.txt");
    assert(z2.getNbEntries(ZipArchive::Current)==4);
    assert(z2.getNbEntries(ZipArchive::Original)==3);
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test14() {
    cout << "Running test 14...";
    
    string c = "some example of text";
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addEntry("test/");
    z1.addData("file/data.txt", c.c_str(), c.length());
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::Write);
    z2.renameEntry(z2.getEntry("file/data.txt"), "content/data/file.txt");
    assert(z2.getNbEntries(ZipArchive::Current)==5);
    assert(z2.getNbEntries(ZipArchive::Original)==3);
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test15() {
    cout << "Running test 15...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata/in/subfolder/data.txt", txtFile, len);
    assert(z1.addEntry("somedata/"));
    assert(z1.addEntry("in/"));
    assert(z1.addEntry("in/subfolder/"));
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    assert(z2.getNbEntries()==6);
    assert(z2.hasEntry("somedata/in/subfolder/data.txt"));
    
    char* data = (char*)z2.readEntry("somedata/in/subfolder/data.txt", true);
    int clen = strlen(data);
    assert(clen==len);
    assert(strncmp(txtFile, data, len)==0);
    
    delete[] data;
    
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test16() {
    cout << "Running test 16...";
    
    string c = "some example of text";
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addEntry("dir/");
    z1.addData("file.txt", c.c_str(), c.length());
    
    ZipEntry e1 = z1.getEntry("dir/");
    ZipEntry e2 = z1.getEntry("file.txt");
    assert(e1.setComment("commentDir"));
    assert(e2.setComment("commentFile"));
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    ZipEntry e12 = z2.getEntry("dir/");
    ZipEntry e22 = z2.getEntry("file.txt");
    assert(e12.getComment() == "commentDir");
    assert(e22.getComment() == "commentFile");
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test17() {
    cout << "Running test 17...";
    
    ZipArchive z1("test.zip");
    assert(z1.open(ZipArchive::ReadOnly) == false);
    void* nil1 = z1.readEntry("an/absent/file.txt", true);
    void* nil2 = z1.readEntry("an/absent/file.txt", true);
    assert(nil1 == NULL);
    assert(nil2 == NULL);
    z1.close();
    z1.unlink();
    
    cout << " done." << endl;
}

void test18() {
    cout << "Running test 18...";
    
    string entry1("a/földér/with/sûbâènts/");
    string entry2("fïle/iñ/sûbfôlder/dàtÄ.txt");
    string text("File wiôth sömè tîxt");
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    assert(z1.addEntry(entry1));
    assert(z1.addData(entry2, text.c_str(), text.length()));
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    ZipEntry e1 = z2.getEntry(entry1);
    assert(!e1.isNull());
    assert(e1.getName() == entry1);
    
    ZipEntry e2 = z2.getEntry(entry2);
    assert(!e2.isNull());
    assert(e2.getName() == entry2);
    assert(e2.readAsText() == text);
    
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test19() {
    cout << "Running test 19...";
    
    const char* txtFile = "this is some data";
    int len = strlen(txtFile);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata", txtFile, len);
    z1.addData("emptydata", "", 0);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    assert(z2.getNbEntries()==2);
    assert(z2.hasEntry("somedata"));
    assert(z2.hasEntry("emptydata"));
    
    ZipEntry entry = z2.getEntry("somedata");
    assert(!entry.isNull());
    
    string data = entry.readAsText(ZipArchive::Current, 4);
    int clen = data.size();
    assert(clen==4);
    assert(strncmp("this", data.c_str(), 4)==0);
    
    string data2 = entry.readAsText(ZipArchive::Current, 1);
    int clen2 = data2.size();
    assert(clen2==1);
    assert(strncmp("t", data2.c_str(), 1)==0);
    
    string data3 = entry.readAsText(ZipArchive::Current, 999);
    int clen3 = data3.size();
    assert(clen3==len);
    assert(strncmp("this is some data", data3.c_str(), len)==0);
    
    ZipEntry entry2 = z2.getEntry("emptydata");
    assert(!entry2.isNull());
    
    std::ofstream file;
    file.open("empty", std::ios_base::out | std::ios_base::binary);
    int ret = z2.readEntry(entry2, file);
    assert(ret == LIBZIPPP_OK);
    #ifndef _WIN32
    assert(file.tellp() == 0);
    #endif
    file.close();
    remove("empty");
    
    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test20() {
    cout << "Running test 20...";

    const char* txtFile = "this is some data";   // 17 Bytes
    const char* txtFile2 = "this is some data!"; // 18 Bytes
    int len = strlen(txtFile);
    int len2 = strlen(txtFile2);
    
    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    z1.addData("somedata", txtFile, len);
    z1.addData("somedata2", txtFile2, len2);
    z1.close();
    
    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    assert(z2.getNbEntries()==2);
    assert(z2.hasEntry("somedata"));
    assert(z2.hasEntry("somedata2"));
    
    ZipEntry entry = z2.getEntry("somedata");
    ZipEntry entry2 = z2.getEntry("somedata2");
    assert(!entry.isNull());
    assert(!entry2.isNull());
    
    {
		// Extract somedata with chunk of 2 bytes, which is not divisible by the file size (17 Bytes)
		std::ofstream ofUnzippedFile("somedata.txt");
		assert(static_cast<bool>(ofUnzippedFile));
		assert(entry.readContent(ofUnzippedFile, ZipArchive::Current, 2) == 0);
		ofUnzippedFile.close();

		std::ifstream ifUnzippedFile("somedata.txt");
		assert(static_cast<bool>(ifUnzippedFile));
		std::string strSomedataText((std::istreambuf_iterator<char>(ifUnzippedFile)), std::istreambuf_iterator<char>());
		assert(strSomedataText.compare(txtFile) == 0);
		ifUnzippedFile.close();
		assert(remove("somedata.txt") == 0);
    }

    {
		// Extract somedata with chunk of 0 bytes (will be defaulted to 512KB).
		std::ofstream ofUnzippedFile("somedata.txt");
		assert(static_cast<bool>(ofUnzippedFile));
		assert(entry.readContent(ofUnzippedFile, ZipArchive::Current, 0) == 0);
		ofUnzippedFile.close();

		std::ifstream ifUnzippedFile("somedata.txt");
		assert(static_cast<bool>(ifUnzippedFile));
		std::string strSomedataText((std::istreambuf_iterator<char>(ifUnzippedFile)), std::istreambuf_iterator<char>());
		assert(strSomedataText.compare(txtFile) == 0);
		ifUnzippedFile.close();
		assert(remove("somedata.txt") == 0);
    }

    {
		// Extract somedata2 with a chunk which is divisible by the size of the file (18 Bytes) to check that the modulo branch
		// is not accessed !
		std::ofstream ofUnzippedFile("somedata2.txt");
		assert(static_cast<bool>(ofUnzippedFile));
		assert(entry2.readContent(ofUnzippedFile, ZipArchive::Current, 2) == 0);
		ofUnzippedFile.close();

		std::ifstream ifUnzippedFile("somedata2.txt");
		assert(static_cast<bool>(ifUnzippedFile));
		std::string strSomedataText((std::istreambuf_iterator<char>(ifUnzippedFile)), std::istreambuf_iterator<char>());
		assert(strSomedataText.compare(txtFile2) == 0);
		ifUnzippedFile.close();
		assert(remove("somedata2.txt") == 0);
    }

    z2.close();
    z2.unlink();
    
    cout << " done." << endl;
}

void test21() {
    cout << "Running test 21..." << endl;
    const char* txtFile = "this is some data";   // 17 Bytes
    const char* txtFile2 = "this is some data!"; // 18 Bytes
    int len = strlen(txtFile);
    int len2 = strlen(txtFile2);

    ZipArchive z1("test.zip");
    z1.setCompressionLevel(8);
    z1.open(ZipArchive::Write);
    z1.addData("somedata", txtFile, len);
    z1.addData("somedata2", txtFile2, len2);
    z1.close();

    std::ifstream ifs("test.zip", std::ios::binary);
    ifs.seekg(0, std::ifstream::end);
    libzippp_uint32 bufferSize = (libzippp_uint32)ifs.tellg();
    char* buffer = (char*)malloc(bufferSize * sizeof(char));
    ifs.seekg(std::ifstream::beg);
    ifs.read(buffer, bufferSize);
    ifs.close();
    
    z1.unlink();

    ZipArchive* z2 = ZipArchive::fromBuffer(buffer, bufferSize);
    assert(!z2->isMutable());
    
    assert(z2->getNbEntries() == 2);
    assert(z2->hasEntry("somedata"));
    assert(z2->hasEntry("somedata2"));

    ZipEntry entry = z2->getEntry("somedata");
    ZipEntry entry2 = z2->getEntry("somedata2");
    assert(!entry.isNull());
    assert(!entry2.isNull());

    int zx2 = z2->close();
    assert(zx2 == LIBZIPPP_OK);
    
    ZipArchive::free(z2);

    const char* txtFile3 = "Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum.";
    const char* txtFile4 = "It is a long established fact that a reader will be distracted by the readable content of a page when looking at its layout. The point of using Lorem Ipsum is that it has a more-or-less normal distribution of letters, as opposed to using 'Content here, content here', making it look like readable English. Many desktop publishing packages and web page editors now use Lorem Ipsum as their default model text, and a search for 'lorem ipsum' will uncover many web sites still in their infancy. Various versions have evolved over the years, sometimes by accident, sometimes on purpose (injected humour and the like).";
    const char* txtFile5 = "Contrary to popular belief, Lorem Ipsum is not simply random text. It has roots in a piece of classical Latin literature from 45 BC, making it over 2000 years old. Richard McClintock, a Latin professor at Hampden-Sydney College in Virginia, looked up one of the more obscure Latin words, consectetur, from a Lorem Ipsum passage, and going through the cites of the word in classical literature, discovered the undoubtable source. Lorem Ipsum comes from sections 1.10.32 and 1.10.33 of 'de Finibus Bonorum et Malorum' (The Extremes of Good and Evil) by Cicero, written in 45 BC. This book is a treatise on the theory of ethics, very popular during the Renaissance. The first line of Lorem Ipsum, 'Lorem ipsum dolor sit amet..', comes from a line in section 1.10.32. The standard chunk of Lorem Ipsum used since the 1500s is reproduced below for those interested. Sections 1.10.32 and 1.10.33 from 'de Finibus Bonorum et Malorum' by Cicero are also reproduced in their exact original form, accompanied by English versions from the 1914 translation by H. Rackham.";
    int len3 = strlen(txtFile3);
    int len4 = strlen(txtFile4);
    int len5 = strlen(txtFile5);

    ZipArchive* z3 = ZipArchive::fromWritableBuffer((void**)&buffer, bufferSize);
    assert(z3->isMutable());
    
    z3->addData("someNewDataFromBuffer.txt", txtFile3, len3);
    z3->addData("newContent/andAnotherFile.txt", txtFile4, len4);
    z3->addData("newContent/yetAnotherContent.txt", txtFile5, len5);

    int zx3 = z3->close();
    assert(zx3 == LIBZIPPP_OK);
    
    libzippp_uint32 newLength = z3->getBufferLength();

    ZipArchive::free(z3);

    ZipArchive* z4 = ZipArchive::fromWritableBuffer((void**)&buffer, newLength);
    assert(z4->getNbEntries() == 6);
    assert(z4->hasEntry("somedata"));
    assert(z4->hasEntry("somedata2"));
    assert(z4->hasEntry("someNewDataFromBuffer.txt"));
    assert(z4->hasEntry("newContent/"));
    assert(z4->hasEntry("newContent/andAnotherFile.txt"));
    assert(z4->hasEntry("newContent/yetAnotherContent.txt"));
    z4->addData("anotherNewDataFromBuffer.txt", txtFile3, len3);
    z4->discard();
    ZipArchive::free(z4);

    free(buffer);

    cout << " done." << endl;
}

void test22() {
    cout << "Running test 22..." << endl;
    const char* content1 = "This is some text that is a little bit longer, so I can test how the progression callback is invoked.";
    const char* content2 = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Phasellus sed metus mollis, facilisis orci vitae, eleifend velit. Quisque et dolor vel nisl gravida vulputate. In iaculis viverra vehicula. Donec viverra euismod odio, sit amet tincidunt nisl aliquam sed. Integer lacinia augue vitae odio varius, at convallis diam molestie. Sed ac nisl at arcu convallis ultricies. Etiam eu metus interdum libero semper vulputate. Proin gravida malesuada justo vel ultrices. Etiam tellus ligula, maximus sed efficitur vitae, iaculis at turpis."
                           "Aliquam eu finibus orci. Quisque maximus enim quis imperdiet vulputate. In vitae velit vel diam scelerisque sollicitudin ac et libero. Donec elit nisi, feugiat ut augue semper, cursus egestas libero. Nullam sed euismod ante. Integer gravida risus nulla, quis vestibulum lacus elementum a. Duis quis vulputate est. Ut elit ipsum, aliquet sit amet gravida et, porttitor quis metus. Vivamus vulputate sed ex ac vulputate. Donec venenatis auctor nulla, quis tempus lorem elementum vel. Suspendisse potenti. In sodales arcu enim, vitae imperdiet quam condimentum sagittis."
                           "Fusce rutrum enim massa, eget ultricies nisi iaculis eu. Quisque erat metus, tempus at volutpat nec, interdum in ligula. Curabitur ullamcorper risus non lobortis vehicula. Proin leo sapien, congue vel metus quis, consectetur lacinia mi. Nunc suscipit erat ipsum, varius commodo risus finibus non. Nam viverra vulputate massa vel pulvinar. Quisque nec quam at lectus sagittis eleifend. Fusce vehicula lectus orci, eu rutrum risus finibus ac. Aenean sit amet mi in velit aliquet faucibus. Donec sit amet diam eget nisl rhoncus posuere eu sodales metus. Integer condimentum placerat neque vitae feugiat. Suspendisse metus velit, faucibus nec hendrerit cursus, pellentesque ut nibh. Duis accumsan mollis elit eget molestie. Donec sit amet congue nibh, quis iaculis lectus. Curabitur placerat sem ex, quis elementum leo sollicitudin sed. Etiam pulvinar turpis vitae ante consequat, vitae eleifend risus dapibus."
                           "Fusce sollicitudin lorem consequat viverra blandit. Praesent feugiat eleifend nibh at eleifend. Etiam quis augue id tortor volutpat placerat. Curabitur id dolor aliquet, consequat eros nec, aliquet velit. Class aptent taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos. Nulla ut nunc ex. Quisque finibus tincidunt sem, sit amet cursus enim hendrerit ut. Donec non interdum augue. Nunc vehicula viverra sem, at scelerisque sapien venenatis vel. Aliquam pretium, enim vel sodales condimentum, quam erat vehicula arcu, et tincidunt tortor justo aliquet lacus. Nulla dignissim pharetra nibh, at varius arcu tincidunt et. Vestibulum viverra velit tristique risus elementum, eu facilisis sapien sollicitudin. Nullam posuere imperdiet nibh sit amet malesuada. Cras ut dolor blandit, interdum lorem auctor, rhoncus dui. Sed mollis, mauris consequat malesuada pulvinar, dui elit pretium est, id euismod est quam in ex."
                           "Ut euismod nisi in leo tempor fringilla. Praesent vel elit et dui facilisis sollicitudin sed non lacus. Etiam tempor ante a tortor pharetra sollicitudin. Nulla facilisi. Sed tincidunt justo urna, sed porttitor dolor aliquam ac. Duis id enim congue, consequat mauris eget, placerat elit. Maecenas eu leo quis tellus eleifend mattis. Suspendisse porttitor suscipit nunc non facilisis. Pellentesque mollis sapien nec purus consectetur interdum. Nunc efficitur neque rhoncus gravida vestibulum. Nullam at aliquet lorem.";
    int len1 = strlen(content1);
    int len2 = strlen(content2);

    ZipArchive z1("test.zip");
    assert(z1.getProgressPrecision() == LIBZIPPP_DEFAULT_PROGRESSION_PRECISION);
    
    SimpleProgressListener spl;
    z1.setProgressPrecision(0);
    z1.addProgressListener(&spl);
    assert(z1.getProgressListeners().size() == 1);
    
    z1.open(ZipArchive::Write);
    z1.addData("somedata", content1, len1);
    z1.addData("somedata2", content2, len2);
    z1.addData("somedata3", content2, len2);
    z1.addData("somedata4", content1, len1);
    z1.addData("somedata5", content1, len1);
    z1.addData("somedata6", content2, len2);
    z1.close();

    assert(z1.getProgressListeners().size() == 1);
    z1.removeProgressListener(&spl);
    assert(z1.getProgressListeners().size() == 0);

    assert(spl.firstValue==0);
    assert(spl.lastValue==1);

    z1.unlink();

    cout << "complete." << endl;
}

void test23() {
    cout << "Running test 23...";
    const char* txtFile = "this is some data";   // 17 Bytes
    const char* txtFile2 = "this is some data!"; // 18 Bytes
    int len = strlen(txtFile);
    int len2 = strlen(txtFile2);

    char* buffer = (char*)calloc(4096, sizeof(char));

    ZipArchive* z1 = ZipArchive::fromWritableBuffer((void**)&buffer, 4096, ZipArchive::New);
    assert(z1!=nullptr);
    z1->addData("somedata", txtFile, len);
    z1->addData("somedata2", txtFile2, len2);
    int rst = z1->close();
    assert(rst==LIBZIPPP_OK);

    /*cout << endl;
    cout << "Buffer data: " << endl;
    for(int i=0 ; i<4096; ++i) {
        char c = buffer[i];
        if(c == '\0') { c = '0'; }
        
        cout << c;
        if((i+1)%8==0) cout << " ";
        if((i+1)%64==0) cout << endl;
    }*/

    int newLength = z1->getBufferLength();
    ZipArchive::free(z1);
    
    ZipArchive* z2 = ZipArchive::fromBuffer(buffer, newLength, true);
    assert(z2!=nullptr);
    assert(z2->getNbEntries() == 2);
    assert(z2->hasEntry("somedata"));
    assert(z2->hasEntry("somedata2"));
    ZipArchive::free(z2);
    
    ZipArchive* z3 = new ZipArchive("within.zip");
    z3->open(ZipArchive::New);
    z3->addData("inside.zip", buffer, newLength);
    z3->close();
    z3->unlink();
    ZipArchive::free(z3);
    
    free(buffer);

    cout << " done." << endl;
    
    /*char buffer[4096] = {};
    for(int i=0 ; i<4096; ++i) {
        buffer[i] = '\0';
    }
    
    zip_error_t error;
    zip_source_t *zs = zip_source_buffer_create(buffer, sizeof(buffer), 0, &error);
    assert(zs!=nullptr);
    
    zip_source_keep(zs);
    
    zip_t * zip = zip_open_from_source(zs, ZIP_TRUNCATE, &error);
    assert(zip!=nullptr);
    
    zip_add_dir(zip, "mydir");
    
    zip_close(zip);
    
    zip_source_open(zs);
    zip_source_read(zs, buffer, sizeof(buffer));
    zip_source_close(zs);
    
    cout << "Buffer data: " << endl;
    for(int i=0 ; i<4096; ++i) {
        char c = buffer[i];
        cout << (int)c;
        if((i+1)%8==0) cout << " ";
        if((i+1)%64==0) cout << endl;
    }*/
}

void test23_2() {
    //important to use calloc/malloc for the fromWritableBuffer !
    void* buffer = calloc(4096, sizeof(char));

    ZipArchive* z1 = ZipArchive::fromWritableBuffer(&buffer, 4096, ZipArchive::New);
    /* add content to the archive */
    
    //will update the content of the buffer
    z1->close();

    //length of the buffer content
    int bufferContentLength = z1->getBufferLength();
    
    ZipArchive::free(z1);

    //read again from the archive:
    ZipArchive* z2 = ZipArchive::fromBuffer(buffer, bufferContentLength);
    /* read the archive - no modification allowed */
    ZipArchive::free(z2);
    
    //read again from the archive, for modification:
    ZipArchive* z3 = ZipArchive::fromWritableBuffer(&buffer, bufferContentLength);
    /* read/write the archive */
    ZipArchive::free(z3);
    
    free(buffer);
}

static void myErrorHandler(const std::string& message,
                           const std::string& strerror,
                           int zip_error_code,
                           int system_error_code)
{
    fprintf(stderr, "# zip_error_code: %d\n", zip_error_code);
    fprintf(stderr, "# system_error_code: %d\n", system_error_code);
    fprintf(stderr, message.c_str(), strerror.c_str());
}

void test24() {
    cout << "Running test 24...";
    
    ZipArchive z1("non-existent.zip");
    z1.setErrorHandlerCallback(myErrorHandler);
    z1.open(ZipArchive::ReadOnly);
    z1.close();

    cout << " done." << endl;
}

/*
 * Regression test for the double-free reported against ZipArchive::close() when an
 * archive opened with fromWritableBuffer is modified and closed (double_free_report.pdf).
 *
 * close() must read the rewritten archive back into a freshly allocated buffer instead
 * of reusing/realloc'ing the very buffer that backs the libzip source. The crafted input
 * below makes the rewrite exceed the original buffer length, which forces the buffer to
 * grow inside close() - the exact path that used to free the source-backed buffer twice.
 *
 * Under AddressSanitizer/valgrind (see the Makefile tests targets), a regression would
 * surface as a double-free / invalid-free here.
 */
void test25() {
    cout << "Running test 25...";

    // 210 bytes - crafted ZIP from double_free_report.pdf:
    //   the local file header declares a filename length of 0x1012 (4114) while the real
    //   name "sample_content.txt" is 18 bytes and the central directory agrees on 18.
    //   This inconsistency drives the rewrite past the original buffer length on close().
    static const unsigned char craftedZip[] = {
        0x50, 0x4b, 0x03, 0x04, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x42,
        0x9c, 0x5a, 0xfa, 0x29, 0x42, 0xbc, 0x18, 0x00, 0x00, 0x00, 0x18, 0x00,
        0x00, 0x00, 0x12, 0x10, 0x1c, 0x00, 0x73, 0x61, 0x6d, 0x70, 0x6c, 0x65,
        0x5f, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2e, 0x74, 0x78, 0x74,
        0x55, 0x54, 0x09, 0x00, 0x03, 0xa8, 0x39, 0x0f, 0x68, 0xa8, 0x39, 0x0f,
        0x68, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04,
        0x00, 0x00, 0x00, 0x00, 0x41, 0x46, 0x4c, 0x2b, 0x2b, 0x20, 0x66, 0x75,
        0x7a, 0x7a, 0x20, 0x74, 0x65, 0x73, 0x74, 0x20, 0x73, 0x61, 0x6d, 0x70,
        0x6c, 0x65, 0x2e, 0x0a, 0x50, 0x4b, 0x01, 0x02, 0x1e, 0x03, 0x0a, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x36, 0x42, 0x9c, 0x5a, 0xfa, 0x29, 0x42, 0xbc,
        0x18, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x12, 0x00, 0x18, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xa4, 0x81, 0x00, 0x00,
        0x00, 0x00, 0x73, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x5f, 0x63, 0x6f, 0x6e,
        0x74, 0x65, 0x6e, 0x74, 0x2e, 0x74, 0x78, 0x74, 0x55, 0x54, 0x05, 0x00,
        0x03, 0xa8, 0x39, 0x0f, 0x68, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x05, 0x06,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x58, 0x00, 0x00, 0x00,
        0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const libzippp_uint32 craftedSize = sizeof(craftedZip);

    // --- exact reproduction of the reported harness: writable buffer (with headroom),
    //     open in Write mode, perform one modification, then close. ---
    {
        libzippp_uint32 bufferSize = craftedSize + 4096;
        void* buffer = calloc(bufferSize, sizeof(char)); //must be malloc/calloc for fromWritableBuffer
        memcpy(buffer, craftedZip, craftedSize);

        ZipArchive* z = ZipArchive::fromWritableBuffer(&buffer, bufferSize, ZipArchive::Write);
        assert(z != nullptr);
        z->addEntry("fuzz_entry/");
        int rst = z->close(); //used to double-free here
        assert(rst == LIBZIPPP_OK);

        //the buffer must remain reopenable after the rewrite (no corruption of the pointer)
        libzippp_uint32 newLength = (libzippp_uint32)z->getBufferLength();
        ZipArchive::free(z);

        ZipArchive* zr = ZipArchive::fromBuffer(buffer, newLength);
        assert(zr != nullptr);
        assert(zr->hasEntry("fuzz_entry/"));
        ZipArchive::free(zr);

        free(buffer); //single owner: the buffer pointer is always valid and freeable
    }

    // --- grow + cloning path: reopen an existing archive in a tight buffer and add an
    //     entry so the rewrite must exceed the original length, forcing the grow loop. ---
    {
        void* buffer = calloc(4096, sizeof(char));
        ZipArchive* z1 = ZipArchive::fromWritableBuffer(&buffer, 4096, ZipArchive::New);
        z1->addData("a.txt", "hello", 5);
        z1->addData("b.txt", "world", 5);
        assert(z1->close() == LIBZIPPP_OK);
        libzippp_uint32 len1 = (libzippp_uint32)z1->getBufferLength();
        ZipArchive::free(z1);

        //reopen at the exact archive size (no headroom) and add ~8KB of incompressible data
        string big;
        big.reserve(8192);
        for (int i = 0; i < 8192; ++i) { big.push_back((char)((i * 2654435761u) >> 13)); }

        ZipArchive* z2 = ZipArchive::fromWritableBuffer(&buffer, len1, ZipArchive::Write);
        assert(z2 != nullptr);
        z2->addData("big.bin", big.data(), big.size());
        assert(z2->close() == LIBZIPPP_OK);
        libzippp_uint32 len2 = (libzippp_uint32)z2->getBufferLength();
        assert(len2 > len1); //the buffer actually grew
        ZipArchive::free(z2);

        ZipArchive* z3 = ZipArchive::fromBuffer(buffer, len2, true);
        assert(z3 != nullptr);
        assert(z3->getNbEntries() == 3);
        assert(z3->hasEntry("a.txt"));
        assert(z3->hasEntry("b.txt"));
        assert(z3->hasEntry("big.bin"));
        assert(z3->getEntry("big.bin").readAsText() == big);
        ZipArchive::free(z3);

        free(buffer);
    }

    cout << " done." << endl;
}

/*
 * Atomic directory rename: a destination occupied by an entry that does not participate in
 * the move must make renameEntry fail with LIBZIPPP_ERROR_UNKNOWN. The conflict check uses
 * the current uncommitted state and must not be limited to the existence of the destination
 * directory entry itself: here new/target/last.txt exists while there is no explicit new/ or
 * new/target/ directory entry. On failure no entry is moved and no directory is created,
 * even after closing and reopening the archive.
 */
void test26() {
    cout << "Running test 26...";

    {
        //build the archive directly with libzip so that new/ and new/target/ stay implicit
        int err = 0;
        zip* zh = zip_open("test.zip", ZIP_CREATE|ZIP_TRUNCATE, &err);
        assert(zh != nullptr);
        addRawEntry(zh, "src/", "");
        addRawEntry(zh, "src/first.txt", "FIRST");
        addRawEntry(zh, "src/last.txt", "LAST");
        addRawEntry(zh, "new/target/last.txt", "TARGET");
        assert(zip_close(zh) == 0);
    }

    ZipArchive z("test.zip");
    z.open(ZipArchive::Write);
    assert(z.getNbEntries() == 4); //no explicit new/ nor new/target/
    assert(!z.hasEntry("new/"));
    assert(!z.hasEntry("new/target/"));

    assert(z.renameEntry("src/", "new/target/") == LIBZIPPP_ERROR_UNKNOWN);

    //the moved entries kept their original names and contents
    assert(z.hasEntry("src/"));
    assert(z.hasEntry("src/first.txt"));
    assert(z.hasEntry("src/last.txt"));
    assert(z.getEntry("src/first.txt").readAsText() == "FIRST");
    assert(z.getEntry("src/last.txt").readAsText() == "LAST");
    //the conflicting entry is untouched
    assert(z.hasEntry("new/target/last.txt"));
    assert(z.getEntry("new/target/last.txt").readAsText() == "TARGET");
    //no parent directory was created by the failed call
    assert(!z.hasEntry("new/"));
    assert(!z.hasEntry("new/target/"));
    assert(z.close() == LIBZIPPP_OK);

    //the failure left no trace on disk either
    ZipArchive r("test.zip");
    r.open(ZipArchive::ReadOnly);
    assert(r.getNbEntries() == 4);
    assert(r.hasEntry("src/") && r.hasEntry("src/first.txt") && r.hasEntry("src/last.txt"));
    assert(r.hasEntry("new/target/last.txt"));
    assert(!r.hasEntry("new/") && !r.hasEntry("new/target/"));
    r.close();
    r.unlink();

    cout << " done." << endl;
}

/*
 * A failed directory rename must roll back to the archive state just before the call (not
 * the state at opening time): previously added entries, replaced contents, modified
 * comments and deletions all survive. The archive stays open and usable, so the rename can
 * be retried with a conflict-free name; everything is then visible after a close/reopen.
 */
void test27() {
    cout << "Running test 27...";

    {
        int err = 0;
        zip* zh = zip_open("test.zip", ZIP_CREATE|ZIP_TRUNCATE, &err);
        assert(zh != nullptr);
        addRawEntry(zh, "src/", "");
        addRawEntry(zh, "src/first.txt", "FIRST");
        addRawEntry(zh, "src/last.txt", "LAST");
        addRawEntry(zh, "new/target/last.txt", "TARGET");
        addRawEntry(zh, "victim.txt", "V");
        assert(zip_close(zh) == 0);
    }

    ZipArchive z("test.zip");
    z.open(ZipArchive::Write);

    //pending modifications performed before the failing rename
    assert(z.addData("added.txt", "A", 1));
    assert(z.addData("src/first.txt", "REPLACED", 8));
    assert(z.getEntry("src/last.txt").setComment("last-comment"));
    assert(z.deleteEntry("victim.txt") == 1);

    assert(z.renameEntry("src/", "new/target/") == LIBZIPPP_ERROR_UNKNOWN);

    //the prior uncommitted modifications are all still there
    assert(z.hasEntry("added.txt"));
    assert(z.getEntry("src/first.txt").readAsText() == "REPLACED");
    assert(z.getEntry("src/last.txt").getComment() == "last-comment");
    assert(!z.hasEntry("victim.txt"));
    //and the failed move left no trace
    assert(z.hasEntry("src/") && z.hasEntry("src/first.txt") && z.hasEntry("src/last.txt"));
    assert(!z.hasEntry("new/") && !z.hasEntry("new/target/"));

    //retry with a free name while the archive is still open
    assert(z.renameEntry("src/", "moved/") == 3);
    assert(z.hasEntry("moved/") && z.hasEntry("moved/first.txt") && z.hasEntry("moved/last.txt"));
    assert(!z.hasEntry("src/"));
    assert(z.getEntry("moved/first.txt").readAsText() == "REPLACED");
    assert(z.getEntry("moved/last.txt").getComment() == "last-comment");
    assert(z.close() == LIBZIPPP_OK);

    ZipArchive r("test.zip");
    r.open(ZipArchive::ReadOnly);
    assert(r.hasEntry("added.txt"));
    assert(r.hasEntry("moved/") && r.hasEntry("moved/first.txt") && r.hasEntry("moved/last.txt"));
    assert(!r.hasEntry("src/") && !r.hasEntry("new/"));
    assert(r.getEntry("moved/first.txt").readAsText() == "REPLACED");
    assert(r.getEntry("moved/last.txt").getComment() == "last-comment");
    assert(r.getEntry("new/target/last.txt").readAsText() == "TARGET");
    assert(!r.hasEntry("victim.txt"));
    r.close();
    r.unlink();

    cout << " done." << endl;
}

/*
 * Moving a directory below its own path is supported. Entries that participate in the move
 * and free their original name must not be considered destination conflicts. Moving a/, a/b/
 * and a/b/file.bin to a/b/ yields a/, a/b/, a/b/b/ and a/b/b/file.bin (the re-created a/
 * parent is not counted), and the returned value is the number of renamed pre-existing
 * entries (3).
 */
void test28() {
    cout << "Running test 28...";

    {
        int err = 0;
        zip* zh = zip_open("test.zip", ZIP_CREATE|ZIP_TRUNCATE, &err);
        assert(zh != nullptr);
        addRawEntry(zh, "a/", "");
        addRawEntry(zh, "a/b/", "");
        addRawEntry(zh, "a/b/file.bin", "BIN");
        assert(zip_close(zh) == 0);
    }

    ZipArchive z("test.zip");
    z.open(ZipArchive::Write);
    ZipEntry dir = z.getEntry("a/");
    assert(!dir.isNull() && dir.isDirectory());
    assert(z.renameEntry(dir, "a/b/") == 3);

    assert(z.hasEntry("a/"));
    assert(z.hasEntry("a/b/"));
    assert(z.hasEntry("a/b/b/"));
    assert(z.hasEntry("a/b/b/file.bin"));
    assert(!z.hasEntry("a/b/file.bin"));
    assert(z.getEntry("a/b/b/file.bin").readAsText() == "BIN");
    assert(z.close() == LIBZIPPP_OK);

    ZipArchive r("test.zip");
    r.open(ZipArchive::ReadOnly);
    assert(r.getNbEntries() == 4);
    assert(r.hasEntry("a/") && r.hasEntry("a/b/") && r.hasEntry("a/b/b/") && r.hasEntry("a/b/b/file.bin"));
    r.close();
    r.unlink();

    cout << " done." << endl;
}

/*
 * Miscellaneous directory rename rules:
 * - an empty directory obeys the same success/failure rules,
 * - the trailing '/' is appended to the destination when missing,
 * - entries with a similar but non-descendant name are not moved,
 * - missing parent directories are created (but not counted),
 * - file content, relative paths and entry comments follow the entries.
 */
void test29() {
    cout << "Running test 29...";

    {
        int err = 0;
        zip* zh = zip_open("test.zip", ZIP_CREATE|ZIP_TRUNCATE, &err);
        assert(zh != nullptr);
        addRawEntry(zh, "empty/", "");
        addRawEntry(zh, "new/target/last.txt", "T");
        assert(zip_close(zh) == 0);
    }

    //empty directory conflicts like any other directory
    ZipArchive ze("test.zip");
    ze.open(ZipArchive::Write);
    assert(ze.renameEntry("empty/", "new/target/") == LIBZIPPP_ERROR_UNKNOWN);
    assert(ze.hasEntry("empty/"));
    assert(!ze.hasEntry("new/") && !ze.hasEntry("new/target/"));
    //no trailing slash on the destination: it is appended
    assert(ze.renameEntry("empty/", "renamed-empty") == 1);
    assert(ze.hasEntry("renamed-empty/"));
    assert(!ze.hasEntry("empty/"));
    ze.close();
    ze.unlink();

    //similar names, parent creation, content and comment preservation
    {
        int err = 0;
        zip* zh = zip_open("test.zip", ZIP_CREATE|ZIP_TRUNCATE, &err);
        assert(zh != nullptr);
        addRawEntry(zh, "d/", "");
        addRawEntry(zh, "d/sub/", "");
        addRawEntry(zh, "d/sub/a.txt", "AAA");
        addRawEntry(zh, "d/b.txt", "BBB");
        addRawEntry(zh, "d-sibling/", "");
        addRawEntry(zh, "d-sibling/f.txt", "SIBLING");
        addRawEntry(zh, "d2.txt", "OTHER");
        assert(zip_close(zh) == 0);
    }

    ZipArchive z("test.zip");
    z.open(ZipArchive::Write);
    assert(z.getEntry("d/sub/a.txt").setComment("comment-a"));
    int renamed = z.renameEntry("d/", "x/y/z/");
    assert(renamed == 4); //d/, d/sub/, d/sub/a.txt, d/b.txt - the created x/, x/y/ do not count

    assert(z.hasEntry("x/") && z.hasEntry("x/y/") && z.hasEntry("x/y/z/"));
    assert(z.hasEntry("x/y/z/sub/a.txt") && z.hasEntry("x/y/z/b.txt"));
    assert(!z.hasEntry("d/") && !z.hasEntry("d/sub/a.txt"));
    assert(z.getEntry("x/y/z/sub/a.txt").readAsText() == "AAA");
    assert(z.getEntry("x/y/z/b.txt").readAsText() == "BBB");
    assert(z.getEntry("x/y/z/sub/a.txt").getComment() == "comment-a");

    //similar names are untouched
    assert(z.hasEntry("d-sibling/") && z.hasEntry("d-sibling/f.txt"));
    assert(z.getEntry("d-sibling/f.txt").readAsText() == "SIBLING");
    assert(z.hasEntry("d2.txt"));
    assert(z.getEntry("d2.txt").readAsText() == "OTHER");
    z.close();

    ZipArchive r("test.zip");
    r.open(ZipArchive::ReadOnly);
    assert(r.getNbEntries() == 9); //4 moved + 2 created parents + d-sibling/ + its file + d2.txt
    r.close();
    r.unlink();

    cout << " done." << endl;
}

/*
 * Failed additions must not leave any trace: when the source file does not exist,
 * addFile returns false and neither the entry nor its (missing) parent directories
 * are created. The archive stays open and usable: other pending changes are preserved
 * and a subsequent valid addition can still be committed with the same close().
 * The historical rejections (not open, read-only, directory name) are unchanged.
 */
void test30() {
    cout << "Running test 30...";

    ZipArchive z0("test.zip");
    z0.open(ZipArchive::Write);
    assert(z0.addData("keep/me.txt", "KEEP", 4));
    assert(z0.close() == LIBZIPPP_OK);

    //not open / read-only / directory name: still rejected
    ZipArchive zc("test.zip");
    assert(!zc.addData("x.txt", "y", 1));
    assert(!zc.addFile("x.txt", "tests.cpp"));
    ZipArchive zr("test.zip");
    zr.open(ZipArchive::ReadOnly);
    assert(!zr.addData("x.txt", "y", 1));
    assert(!zr.addFile("x.txt", "tests.cpp"));
    assert(zr.close() == LIBZIPPP_OK);

    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    assert(!z1.addData("dir/", "y", 1)); //a directory name is not a file
    assert(!z1.addFile("dir/", "tests.cpp"));

    //missing source file: false, and no trace left behind
    assert(!z1.addFile("newdir/sub/missing.txt", "this-file-does-not-exist.bin"));
    assert(!z1.hasEntry("newdir/sub/missing.txt"));
    assert(!z1.hasEntry("newdir/"));
    assert(!z1.hasEntry("newdir/sub/"));
    assert(z1.getNbEntries(ZipArchive::Current)==2); //keep/ and keep/me.txt only

    //the archive is still open and usable: the same entry can be written validly
    assert(z1.isOpen());
    assert(z1.addData("newdir/sub/missing.txt", "NOW", 3));
    assert(z1.getEntry("newdir/sub/missing.txt").readAsText() == "NOW");
    assert(z1.close() == LIBZIPPP_OK);

    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    assert(z2.getEntry("keep/me.txt").readAsText() == "KEEP");
    assert(z2.getEntry("newdir/sub/missing.txt").readAsText() == "NOW");
    z2.close();
    z2.unlink();

    cout << " done." << endl;
}

/*
 * addData input validation: a null data pointer with a non-zero length is rejected
 * before the archive is touched (no entry, no parent directory), while a null data
 * pointer with a zero length still writes an empty file.
 */
void test31() {
    cout << "Running test 31...";

    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);
    assert(!z1.addData("dir/rejected.txt", nullptr, 10));
    assert(!z1.hasEntry("dir/rejected.txt"));
    assert(!z1.hasEntry("dir/"));
    assert(z1.getNbEntries(ZipArchive::Current)==0);
    assert(z1.addData("dir/empty.txt", nullptr, 0));
    assert(z1.close() == LIBZIPPP_OK);

    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    assert(!z2.hasEntry("dir/rejected.txt"));
    assert(z2.hasEntry("dir/"));
    assert(z2.hasEntry("dir/empty.txt"));
    ZipEntry empty = z2.getEntry("dir/empty.txt");
    assert(!empty.isNull());
    assert(empty.getSize()==0);
    assert(empty.readAsText().empty());
    z2.close();
    z2.unlink();

    cout << " done." << endl;
}

/*
 * A failed overwrite must preserve the exact pre-call state: a previous (uncommitted)
 * replacement of the same entry, its comment, its compression settings, the other
 * pending additions/deletions/renames and the archive comment all survive. Existing
 * parent directories are never removed. The archive stays open, previously obtained
 * ZipEntry objects remain usable and a valid overwrite can still be committed.
 */
void test32() {
    cout << "Running test 32...";

    ZipArchive z0("test.zip");
    z0.open(ZipArchive::Write);
    z0.addData("dir/file.txt", "ORIGINAL", 8);
    z0.addData("dir/comp.txt", "COMP", 4);
    z0.addData("dir/other.txt", "OTHER", 5);
    z0.addData("victim.txt", "V", 1);
    assert(z0.close() == LIBZIPPP_OK);

    ZipArchive z1("test.zip");
    z1.open(ZipArchive::Write);

    //pending changes staged before the failing call
    assert(z1.addData("dir/file.txt", "REPLACED", 8)); //uncommitted overwrite
    ZipEntry f = z1.getEntry("dir/file.txt");
    assert(!f.isNull());
    assert(f.setComment("my-comment"));
    ZipEntry fc = z1.getEntry("dir/comp.txt");
    assert(!fc.isNull());
    assert(fc.setCompressionMethod(DEFLATE)); //staged compression change on another entry
    assert(z1.addData("added.txt", "A", 1));
    assert(z1.deleteEntry("victim.txt")==1);
    assert(z1.renameEntry("dir/other.txt", "dir/moved.txt")==1);
    assert(z1.setComment("archive-comment"));

    //the failing overwrite (missing source file) must not change anything
    assert(!z1.addFile("dir/file.txt", "no-such-source-file.bin"));
    assert(z1.isOpen());

    //the previously obtained entry is still usable and shows the staged changes
    assert(f.readAsText() == "REPLACED");
    assert(f.getComment() == "my-comment");
    ZipEntry f2 = z1.getEntry("dir/file.txt");
    assert(f2.readAsText() == "REPLACED");
    assert(f2.getComment() == "my-comment");
    //the staged compression change is untouched by the failed call
    assert(z1.getEntry("dir/comp.txt").getCompressionMethod()==DEFLATE);
    //all the other pending changes are preserved
    assert(z1.hasEntry("added.txt"));
    assert(!z1.hasEntry("victim.txt"));
    assert(z1.hasEntry("dir/moved.txt"));
    assert(!z1.hasEntry("dir/other.txt"));
    assert(z1.getComment() == "archive-comment");
    assert(z1.hasEntry("dir/")); //existing parent directory untouched

    //a valid overwrite is still possible and commits with everything else
    assert(z1.addData("dir/file.txt", "FINAL", 5));
    assert(z1.close() == LIBZIPPP_OK);

    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    ZipEntry r = z2.getEntry("dir/file.txt");
    assert(r.readAsText() == "FINAL");
    assert(r.getComment() == "my-comment");
    ZipEntry rc = z2.getEntry("dir/comp.txt");
    assert(rc.getCompressionMethod()==DEFLATE);
    assert(rc.readAsText() == "COMP");
    assert(z2.hasEntry("added.txt"));
    assert(!z2.hasEntry("victim.txt"));
    assert(z2.getEntry("dir/moved.txt").readAsText() == "OTHER");
    assert(z2.getComment() == "archive-comment");
    z2.close();
    z2.unlink();

    cout << " done." << endl;
}

/*
 * Ownership of the data buffer with freeData=true: the memory must be freeable with
 * free(). On success the archive owns it and releases it exactly once, whether the
 * changes are committed, overwritten again or discarded; on failure the caller keeps
 * the ownership and may free it immediately. freeData=false leaves the ownership to
 * the caller in every case. Run under valgrind/ASAN (see the Makefile tests targets):
 * a regression surfaces as a double-free, invalid free or leak here.
 */
void test33() {
    cout << "Running test 33...";

    //success + successive overwrite + commit: each buffer is freed exactly once
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::Write);
        char* data1 = (char*)malloc(6);
        memcpy(data1, "HELLO", 6);
        assert(z.addData("a/b.txt", data1, 5, true));
        char* data2 = (char*)malloc(6);
        memcpy(data2, "WORLD", 6);
        assert(z.addData("a/b.txt", data2, 5, true)); //replaces the previous buffer
        assert(z.close() == LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.getEntry("a/b.txt").readAsText() == "WORLD");
        r.close();
        r.unlink();
    }

    //discard: the adopted buffer is released without being committed
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::Write);
        char* data = (char*)malloc(4);
        memcpy(data, "DIS", 4);
        assert(z.addData("gone.txt", data, 3, true));
        z.discard();
        assert(!z.isOpen());
    }

    //failure: the caller keeps the ownership and frees the buffer itself
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::Write);
        char* rejected = (char*)malloc(8);
        memcpy(rejected, "MINE", 4);
        assert(!z.addData("bad.txt", nullptr, 10, true)); //rejected before any adoption
        free(rejected); //must not have been freed by the archive
        //freeData=false: the caller owns the memory until the commit
        char* kept = (char*)malloc(5);
        memcpy(kept, "KEPT", 5);
        assert(z.addData("kept.txt", kept, 4, false));
        assert(z.close() == LIBZIPPP_OK);
        free(kept); //valid until here: the commit is done

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.getEntry("kept.txt").readAsText() == "KEPT");
        assert(!r.hasEntry("bad.txt"));
        r.close();
        r.unlink();
    }

    cout << " done." << endl;
}

/*
 * The compression and encryption methods selected on the archive are applied to the
 * added entries: after a commit, reopening the archive shows the same methods and the
 * same content. (The encryption part requires libzippp to be built with
 * LIBZIPPP_WITH_ENCRYPTION.)
 */
void test34() {
    cout << "Running test 34...";

    ZipArchive z1("test.zip");
    z1.setCompressionMethod(DEFLATE);
    z1.open(ZipArchive::Write);
    assert(z1.addData("c/file.txt", "DATA", 4));
    assert(z1.close() == LIBZIPPP_OK);

    ZipArchive z2("test.zip");
    z2.open(ZipArchive::ReadOnly);
    ZipEntry c = z2.getEntry("c/file.txt");
    assert(!c.isNull());
    assert(c.getCompressionMethod()==DEFLATE);
    assert(c.readAsText() == "DATA");
    z2.close();
    z2.unlink();

#ifdef LIBZIPPP_WITH_ENCRYPTION
    ZipArchive z3("test.zip", "password", ZipArchive::Aes256);
    z3.open(ZipArchive::Write);
    assert(z3.addData("secret/data.txt", "TOPSECRET", 9));
    assert(z3.close() == LIBZIPPP_OK);

    ZipArchive z4("test.zip", "password", ZipArchive::Aes256);
    z4.open(ZipArchive::ReadOnly);
    ZipEntry s = z4.getEntry("secret/data.txt");
    assert(!s.isNull());
    assert(s.getEncryptionMethod()==ZIP_EM_AES_256);
    assert(s.readAsText() == "TOPSECRET");
    z4.close();
    z4.unlink();

    //a failed overwrite on an encrypted archive preserves the staged encrypted
    //replacement, its comment and its encryption settings
    ZipArchive z5("test.zip", "password", ZipArchive::Aes256);
    z5.open(ZipArchive::Write);
    assert(z5.addData("s.txt", "ONE", 3));
    assert(z5.close() == LIBZIPPP_OK);

    ZipArchive z6("test.zip", "password", ZipArchive::Aes256);
    z6.open(ZipArchive::Write);
    assert(z6.addData("s.txt", "TWO", 3)); //staged encrypted replacement
    ZipEntry se = z6.getEntry("s.txt");
    assert(se.setComment("secret-comment"));
    assert(!z6.addFile("s.txt", "no-such-file.bin")); //failed overwrite
    assert(se.readAsText() == "TWO");
    assert(se.getComment() == "secret-comment");
    assert(z6.close() == LIBZIPPP_OK);

    ZipArchive z7("test.zip", "password", ZipArchive::Aes256);
    z7.open(ZipArchive::ReadOnly);
    ZipEntry se2 = z7.getEntry("s.txt");
    assert(se2.readAsText() == "TWO");
    assert(se2.getComment() == "secret-comment");
    assert(se2.getEncryptionMethod()==ZIP_EM_AES_256);
    z7.close();
    z7.unlink();
#endif

    cout << " done." << endl;
}

/*
 * A streambuf that only produces characters, without any positioning support:
 * reading works from the current position to the end of the content.
 */
class NonSeekableBuf : public streambuf {
public:
    NonSeekableBuf(const string& content) : content(content), pos(0) {}
protected:
    virtual int_type underflow() {
        if (pos>=content.size()) { return traits_type::eof(); }
        return traits_type::to_int_type(content[pos]);
    }
    virtual int_type uflow() {
        if (pos>=content.size()) { return traits_type::eof(); }
        return traits_type::to_int_type(content[pos++]);
    }
private:
    string content;
    size_t pos;
};

/*
 * A streambuf that produces `limit` bytes and then fails: the thrown error is
 * turned into badbit by the istream machinery (a genuine read error, not an
 * end-of-stream).
 */
class FailingBuf : public streambuf {
public:
    FailingBuf(const string& content, size_t limit) : content(content), pos(0), limit(limit) {}
protected:
    virtual int_type underflow() {
        if (pos>=limit) { throw runtime_error("read failure"); }
        if (pos>=content.size()) { return traits_type::eof(); }
        return traits_type::to_int_type(content[pos]);
    }
    virtual int_type uflow() {
        if (pos>=limit) { throw runtime_error("read failure"); }
        if (pos>=content.size()) { return traits_type::eof(); }
        return traits_type::to_int_type(content[pos++]);
    }
private:
    string content;
    size_t pos;
    size_t limit;
};

class CancellableProgressListener : public ZipProgressListener {
public:
    CancellableProgressListener(void) : cancelled(false) {}
    bool cancelled;
    void progression(double) {}
    int cancel() { return cancelled ? 1 : 0; }
};

/*
 * ZipArchive::addData(entryName, std::istream&): the content is read from the
 * current position of the stream to its normal end (no length, no seeking
 * needed), staged independently of the stream and committed by close().
 */
void test35() {
    cout << "Running test 35...";

    //binary content (zero bytes included), automatic parent directories, accurate
    //Current state before the commit, Original state untouched, and a commit done
    //after the stream was destroyed
    string content;
    for (size_t i=0 ; i<300000 ; ++i) { content.push_back((char)(i%251)); }
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        {
            istringstream in(string(content.data(), content.size()), ios::binary);
            assert(z.addData("deeply/nested/dirs/data.bin", in));
        } //the stream is destroyed here, long before the commit
        assert(z.hasEntry("deeply/"));
        assert(z.hasEntry("deeply/nested/"));
        assert(z.hasEntry("deeply/nested/dirs/"));
        ZipEntry e = z.getEntry("deeply/nested/dirs/data.bin");
        assert(!e.isNull());
        assert(e.getSize()==content.size()); //accurate length before the commit
        string current = e.readAsText();
        assert(current.size()==content.size());
        assert(memcmp(current.data(), content.data(), content.size())==0);
        //the new entry does not exist in the Original state
        assert(!z.hasEntry("deeply/nested/dirs/data.bin", false, true, ZipArchive::Original));
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        ZipEntry re = r.getEntry("deeply/nested/dirs/data.bin");
        assert(!re.isNull());
        assert(re.getSize()==content.size());
        libzippp_uint8* raw = re.readAsBinary();
        assert(raw!=nullptr);
        assert(memcmp(raw, content.data(), content.size())==0);
        delete[] raw;
        r.close();
        r.unlink();
    }

    //an empty stream successfully writes an empty file
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        istringstream in("");
        assert(z.addData("empty.bin", in));
        ZipEntry e = z.getEntry("empty.bin");
        assert(!e.isNull());
        assert(e.getSize()==0);
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.hasEntry("empty.bin"));
        assert(r.getEntry("empty.bin").getSize()==0);
        r.close();
        r.unlink();
    }

    //a non-seekable stream is read from its current position to its end
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        NonSeekableBuf buf("unseekable content");
        istream in(&buf);
        assert(z.addData("noseek.txt", in));
        assert(z.getEntry("noseek.txt").readAsText()=="unseekable content");
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.getEntry("noseek.txt").readAsText()=="unseekable content");
        r.close();
        r.unlink();
    }

    //the reading starts at the current position of the stream, not at its beginning
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        istringstream in("SKIPrest");
        in.seekg(4);
        assert(z.addData("pos.txt", in));
        assert(z.getEntry("pos.txt").readAsText()=="rest");
        assert(z.close()==LIBZIPPP_OK);
        z.unlink();
    }

    //a file stream works like any other input stream
    {
        {
            ofstream out("source.bin", ios::binary);
            string fileContent;
            for (size_t i=0 ; i<10000 ; ++i) { fileContent.push_back((char)(i%239)); }
            out.write(fileContent.data(), fileContent.size());
        }
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        ifstream in("source.bin", ios::binary);
        assert(z.addData("fromfile.bin", in));
        in.close(); //the stream may be closed right after the call
        assert(z.close()==LIBZIPPP_OK);

        ifstream expected("source.bin", ios::binary);
        string expectedContent((istreambuf_iterator<char>(expected)), istreambuf_iterator<char>());
        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        ZipEntry re = r.getEntry("fromfile.bin");
        assert(re.getSize()==expectedContent.size());
        assert(re.readAsText()==expectedContent);
        r.close();
        r.unlink();
        remove("source.bin");
    }

    //a large content with a small chunk size is staged and committed whole
    {
        string big;
        big.reserve(3*1024*1024);
        for (size_t i=0 ; i<3*1024*1024 ; ++i) { big.push_back((char)((i*31+i/7)%253)); }
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        {
            istringstream in(string(big.data(), big.size()), ios::binary);
            assert(z.addData("big.bin", in, 4096)); //tiny chunks on purpose
        }
        ZipEntry e = z.getEntry("big.bin");
        assert(!e.isNull());
        assert(e.getSize()==big.size());
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        ZipEntry re = r.getEntry("big.bin");
        assert(re.getSize()==big.size());
        libzippp_uint8* raw = re.readAsBinary();
        assert(raw!=nullptr);
        assert(memcmp(raw, big.data(), big.size())==0);
        delete[] raw;
        r.close();
        r.unlink();
    }

    //successive overwrites of the same entry (stream over buffer, stream over
    //stream, buffer over stream): the last content wins, nothing leaks
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        assert(z.addData("e.txt", "BUFFER", 6));
        { istringstream in("STREAM1"); assert(z.addData("e.txt", in)); }
        { istringstream in("STREAM2-LONGER"); assert(z.addData("e.txt", in)); }
        assert(z.getEntry("e.txt").readAsText()=="STREAM2-LONGER");
        assert(z.addData("e.txt", "FINAL", 5));
        assert(z.getEntry("e.txt").readAsText()=="FINAL");
        { istringstream in("VERY-FINAL"); assert(z.addData("e.txt", in)); }
        assert(z.getEntry("e.txt").readAsText()=="VERY-FINAL");
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.getEntry("e.txt").readAsText()=="VERY-FINAL");
        r.close();
        r.unlink();
    }

    //overwriting a committed entry: the Original state still reflects the opened
    //content until the commit
    {
        { ZipArchive w("test.zip"); w.open(ZipArchive::New); assert(w.addData("f.txt", "COMMITTED", 9)); assert(w.close()==LIBZIPPP_OK); }
        ZipArchive z("test.zip");
        z.open(ZipArchive::Write);
        istringstream in("STAGED");
        assert(z.addData("f.txt", in));
        ZipEntry e = z.getEntry("f.txt");
        assert(e.readAsText()=="STAGED");
        assert(e.readAsText(ZipArchive::Original)=="COMMITTED");
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.getEntry("f.txt").readAsText()=="STAGED");
        r.close();
        r.unlink();
    }

    //a stream-staged entry can be deleted before the commit
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        { istringstream in("GONE"); assert(z.addData("gone.txt", in)); }
        { istringstream in("KEPT"); assert(z.addData("kept.txt", in)); }
        assert(z.deleteEntry("gone.txt")==1);
        assert(!z.hasEntry("gone.txt"));
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(!r.hasEntry("gone.txt"));
        assert(r.getEntry("kept.txt").readAsText()=="KEPT");
        r.close();
        r.unlink();
    }

    //a stream-staged entry can be renamed before the commit
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        { istringstream in("MOVABLE"); assert(z.addData("before.txt", in)); }
        assert(z.renameEntry("before.txt", "after.txt")==1);
        assert(z.getEntry("after.txt").readAsText()=="MOVABLE");
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(!r.hasEntry("before.txt"));
        assert(r.getEntry("after.txt").readAsText()=="MOVABLE");
        r.close();
        r.unlink();
    }

    //the compression method selected on the archive applies to stream writes
    {
        ZipArchive z("test.zip");
        z.setCompressionMethod(DEFLATE);
        z.open(ZipArchive::New);
        string compressible(20000, 'x');
        istringstream in(compressible);
        assert(z.addData("compressed.txt", in));
        ZipEntry e = z.getEntry("compressed.txt");
        assert(e.getCompressionMethod()==DEFLATE);
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        ZipEntry re = r.getEntry("compressed.txt");
        assert(re.getCompressionMethod()==DEFLATE);
        assert(re.readAsText()==compressible);
        r.close();
        r.unlink();
    }

#ifdef LIBZIPPP_WITH_ENCRYPTION
    //the encryption selected on the archive applies to stream writes
    {
        ZipArchive z("test.zip", "password", ZipArchive::Aes256);
        z.open(ZipArchive::New);
        istringstream in("TOPSECRET");
        assert(z.addData("secret.txt", in));
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip", "password", ZipArchive::Aes256);
        r.open(ZipArchive::ReadOnly);
        ZipEntry re = r.getEntry("secret.txt");
        assert(!re.isNull());
        assert(re.getEncryptionMethod()==ZIP_EM_AES_256);
        assert(re.readAsText()=="TOPSECRET");
        r.close();
        r.unlink();
    }
#endif

    cout << " done." << endl;
}

/*
 * ZipArchive::addData(entryName, std::istream&): rejections without consuming
 * the stream, atomicity of read failures and commit control (cancel/discard).
 */
void test36() {
    cout << "Running test 36...";

    //rejections that must not consume anything from the stream
    {
        ZipArchive z("test.zip");
        istringstream in1("abc");
        assert(!z.addData("x.txt", in1)); //not open
        char c = 0;
        assert(in1.get(c) && c=='a');

        { ZipArchive w("test.zip"); w.open(ZipArchive::New); assert(w.addData("a.txt", "z", 1)); assert(w.close()==LIBZIPPP_OK); }
        assert(z.open(ZipArchive::ReadOnly));
        istringstream in2("abc");
        assert(!z.addData("x.txt", in2)); //read-only
        c = 0; assert(in2.get(c) && c=='a');
        z.close();

        assert(z.open(ZipArchive::Write));
        istringstream in3("abc");
        assert(!z.addData("", in3)); //empty name
        assert(!z.addData("dir/", in3)); //directory name
        c = 0; assert(in3.get(c) && c=='a');
        assert(z.close()==LIBZIPPP_OK);
        z.unlink();
    }

    //a read error leaves the archive exactly as it was: the previously staged
    //replacement of the same entry, its comment, the other pending changes are
    //preserved and no parent directory is left behind
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        assert(z.addData("keep.txt", "KEEP", 4));
        assert(z.addData("same.txt", "OLD", 3));
        ZipEntry same = z.getEntry("same.txt");
        assert(same.setComment("same-comment"));

        string payload("PAYLOAD");
        FailingBuf failing(payload, 2); //fails after 2 bytes
        istream in(&failing);
        assert(!z.addData("same.txt", in));
        assert(same.readAsText()=="OLD");
        assert(same.getComment()=="same-comment");
        assert(z.getEntry("keep.txt").readAsText()=="KEEP");

        FailingBuf failing2(payload, 2);
        istream in2(&failing2);
        assert(!z.addData("newdir/sub/file.txt", in2));
        assert(!z.hasEntry("newdir/")); //no parent directory left behind
        assert(!z.hasEntry("newdir/sub/file.txt"));

        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.getEntry("same.txt").readAsText()=="OLD");
        assert(r.getEntry("same.txt").getComment()=="same-comment");
        assert(r.getEntry("keep.txt").readAsText()=="KEEP");
        assert(!r.hasEntry("newdir/"));
        r.close();
        r.unlink();
    }

    //an exception thrown by the stream at its normal end is a completed read
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        istringstream in("hello world");
        in.exceptions(ios::eofbit | ios::failbit);
        assert(z.addData("exc.txt", in));
        assert(z.getEntry("exc.txt").readAsText()=="hello world");
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.getEntry("exc.txt").readAsText()=="hello world");
        r.close();
        r.unlink();
    }

    //a genuine read error signalled by an exception: false is returned, nothing
    //is propagated to the caller
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        string payload("PAYLOAD");
        FailingBuf failing(payload, 2);
        istream in(&failing);
        in.exceptions(ios::badbit);
        assert(!z.addData("boom.txt", in)); //must return false, not throw
        assert(!z.hasEntry("boom.txt"));
        assert(z.close()==LIBZIPPP_OK);
        z.unlink();
    }

    //a cancelled commit keeps the archive and the staged content valid; once the
    //cancellation is lifted, the next close commits everything, even after the
    //stream is gone
    {
        ZipArchive z("test.zip");
        z.open(ZipArchive::New);
        istringstream* in = new istringstream("streamed content");
        assert(z.addData("cancel/data.txt", *in));
        CancellableProgressListener listener;
        listener.cancelled = true;
        z.addProgressListener(&listener);
        assert(z.close()!=LIBZIPPP_OK); //cancelled: nothing committed
        assert(z.isOpen());
        assert(z.getEntry("cancel/data.txt").readAsText()=="streamed content");
        delete in; //the stream is destroyed before the commit
        listener.cancelled = false;
        assert(z.close()==LIBZIPPP_OK);

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.getEntry("cancel/data.txt").readAsText()=="streamed content");
        r.close();
        r.unlink();
    }

    //discard abandons the staged content: the file on disk is unchanged
    {
        { ZipArchive w("test.zip"); w.open(ZipArchive::New); assert(w.addData("orig.txt", "ORIG", 4)); assert(w.close()==LIBZIPPP_OK); }
        ZipArchive z("test.zip");
        z.open(ZipArchive::Write);
        istringstream in("DISCARDED");
        assert(z.addData("orig.txt", in));
        assert(z.getEntry("orig.txt").readAsText()=="DISCARDED");
        z.discard();

        ZipArchive r("test.zip");
        r.open(ZipArchive::ReadOnly);
        assert(r.getEntry("orig.txt").readAsText()=="ORIG");
        r.close();
        r.unlink();
    }

    cout << " done." << endl;
}

/*
 * A saved ZipEntry keeps designating the entry it was obtained for throughout the opening,
 * even after that entry was renamed and even when its old name is reused by a newly added
 * entry: renameEntry/deleteEntry (ZipEntry overload) anchor on the stable libzip slot, not
 * on the name captured in the object. Copies of an entry share the same identity, so the
 * object never has to be fetched again after a move. A deleted entry - even with its old
 * name taken over - is reported as LIBZIPPP_ERROR_INVALID_ENTRY while all pending changes
 * are preserved; renaming to the entry's current name is LIBZIPPP_ERROR_INVALID_PARAMETER;
 * a destination conflict still returns LIBZIPPP_ERROR_UNKNOWN and leaves the archive in
 * its pre-call state, after which the object remains usable. Which children follow a moved
 * directory is evaluated against the Current state at call time.
 */
void test37() {
    cout << "Running test 37...";

    {
        int err = 0;
        zip* zh = zip_open("test.zip", ZIP_CREATE|ZIP_TRUNCATE, &err);
        assert(zh != nullptr);
        addRawEntry(zh, "a/", "");
        addRawEntry(zh, "a/f.txt", "F");
        addRawEntry(zh, "a/keep.txt", "KEEP");
        addRawEntry(zh, "a-sibling/x.txt", "SIBLING");
        addRawEntry(zh, "plain.txt", "PLAIN");
        assert(zip_close(zh) == 0);
    }

    ZipArchive z("test.zip");
    z.open(ZipArchive::Write);

    //objects saved before any rename (including a copy that is used later)
    ZipEntry dir = z.getEntry("a/");
    ZipEntry file = z.getEntry("a/f.txt");
    ZipEntry dirCopy = dir;
    ZipEntry plain = z.getEntry("plain.txt");
    assert(!dir.isNull() && dir.isDirectory());
    assert(!file.isNull() && file.isFile());

    //move a/ to b/ through the saved directory object
    assert(z.renameEntry(dir, "b/") == 3);
    assert(z.hasEntry("b/") && z.hasEntry("b/f.txt") && z.hasEntry("b/keep.txt"));
    assert(!z.hasEntry("a/"));
    //the saved file object already follows the move when reading
    assert(file.readAsText() == "F");
    //a merely similar neighbour never followed
    assert(z.hasEntry("a-sibling/x.txt"));

    //a brand new a/ (with a new file reusing the saved object's old name) is unrelated
    assert(z.addData("a/f.txt", "NEWA", 4));
    assert(z.addData("a/h.txt", "H", 1));
    assert(z.getEntry("a/f.txt").readAsText() == "NEWA");

    //renaming the saved directory moves b/'s current contents; the new a/ stays put
    assert(z.renameEntry(dir, "c/") == 3);
    assert(z.hasEntry("c/") && z.hasEntry("c/f.txt") && z.hasEntry("c/keep.txt"));
    assert(!z.hasEntry("b/"));
    assert(z.hasEntry("a/") && z.hasEntry("a/f.txt") && z.hasEntry("a/h.txt"));
    assert(z.getEntry("a/f.txt").readAsText() == "NEWA");
    //the saved file object designates c/f.txt
    assert(file.readAsText() == "F");

    //it can be renamed on its own; renaming to its current name is invalid
    assert(z.renameEntry(file, "c/f2.txt") == 1);
    assert(z.renameEntry(file, "c/f2.txt") == LIBZIPPP_ERROR_INVALID_PARAMETER);
    assert(z.hasEntry("c/f2.txt") && !z.hasEntry("c/f.txt"));
    //a copy of the object designates the very same entry
    assert(z.renameEntry(file, "c/f.txt") == 1);

    //the by-name overload still resolves the CURRENT name (the new a/f.txt here)
    assert(z.renameEntry("a/f.txt", "a/f3.txt") == 1);
    assert(z.hasEntry("a/f3.txt") && !z.hasEntry("a/f.txt"));

    //destination conflict: atomic failure, object stays usable
    istringstream streamed("STREAMED");
    assert(z.addData("stream.txt", streamed));
    assert(z.renameEntry(dir, "a/") == LIBZIPPP_ERROR_UNKNOWN);
    //pre-call state restored: c/ still there, new a/ untouched, no merge
    assert(z.hasEntry("c/") && z.hasEntry("c/f.txt") && z.hasEntry("c/keep.txt"));
    assert(z.hasEntry("a/") && z.hasEntry("a/f3.txt") && z.hasEntry("a/h.txt"));
    //other pending modifications are preserved
    assert(z.getEntry("stream.txt").readAsText() == "STREAMED");
    //the saved object remains usable for a legal operation
    assert(z.renameEntry(dir, "d/") == 3);
    assert(z.hasEntry("d/") && z.hasEntry("d/f.txt") && z.hasEntry("d/keep.txt"));
    assert(!z.hasEntry("c/"));
    //a child joined after the move follows the saved directory; one moved out does not
    assert(z.addData("d/late.txt", "L", 1));
    assert(z.renameEntry("d/keep.txt", "kept-out.txt") == 1);

    //the directory copy saved before all the renames designates the same directory
    assert(z.renameEntry(dirCopy, "e/") == 3); //d/, d/f.txt, d/late.txt (keep.txt moved out)
    assert(z.hasEntry("e/") && z.hasEntry("e/f.txt") && z.hasEntry("e/late.txt"));
    assert(!z.hasEntry("e/keep.txt"));
    assert(z.hasEntry("kept-out.txt"));

    //delete the saved file object: only e/f.txt goes; the object is then invalid
    assert(z.deleteEntry(file) == 1);
    assert(!z.hasEntry("e/f.txt"));
    assert(z.deleteEntry(file) == LIBZIPPP_ERROR_INVALID_ENTRY);
    assert(z.renameEntry(file, "x.txt") == LIBZIPPP_ERROR_INVALID_ENTRY);

    //delete the saved directory object: only its current directory and children
    assert(z.deleteEntry(dir) == 2); //e/ and e/late.txt
    assert(!z.hasEntry("e/") && !z.hasEntry("e/late.txt"));
    assert(z.deleteEntry(dir) == LIBZIPPP_ERROR_INVALID_ENTRY);
    assert(z.renameEntry(dir, "z/") == LIBZIPPP_ERROR_INVALID_ENTRY);
    //reusing the deleted directory's old name must not "revive" the object
    assert(z.addData("e/new.txt", "EN", 2));
    assert(z.deleteEntry(dir) == LIBZIPPP_ERROR_INVALID_ENTRY);
    assert(z.hasEntry("e/new.txt"));

    //same rule for a regular file whose old name is reused
    assert(z.renameEntry(plain, "plain1.txt") == 1);
    assert(z.addData("plain.txt", "NEWPLAIN", 8));
    assert(z.deleteEntry(plain) == 1); //deletes plain1.txt, never the new plain.txt
    assert(!z.hasEntry("plain1.txt"));
    assert(z.getEntry("plain.txt").readAsText() == "NEWPLAIN");

    assert(z.close() == LIBZIPPP_OK);

    //final state after a close/reopen
    ZipArchive r("test.zip");
    r.open(ZipArchive::ReadOnly);
    assert(r.hasEntry("a/") && r.hasEntry("a/f3.txt") && r.hasEntry("a/h.txt"));
    assert(r.getEntry("a/f3.txt").readAsText() == "NEWA");
    assert(r.hasEntry("e/new.txt") && r.getEntry("e/new.txt").readAsText() == "EN");
    assert(r.hasEntry("plain.txt") && r.getEntry("plain.txt").readAsText() == "NEWPLAIN");
    assert(r.hasEntry("kept-out.txt") && r.getEntry("kept-out.txt").readAsText() == "KEEP");
    assert(r.hasEntry("stream.txt") && r.getEntry("stream.txt").readAsText() == "STREAMED");
    assert(r.hasEntry("a-sibling/x.txt"));
    assert(!r.hasEntry("b/") && !r.hasEntry("c/") && !r.hasEntry("d/") && !r.hasEntry("e/late.txt"));
    r.close();
    r.unlink();

    cout << " done." << endl;
}

/*
 * Moving a saved directory below its own path and then deleting it through the saved
 * object: the auto-recreated same-named parent directory (and entries later placed in it)
 * is a different entry and must survive; only the moved directory and its current
 * descendants are removed.
 */
void test38() {
    cout << "Running test 38...";

    {
        int err = 0;
        zip* zh = zip_open("test.zip", ZIP_CREATE|ZIP_TRUNCATE, &err);
        assert(zh != nullptr);
        addRawEntry(zh, "a/", "");
        addRawEntry(zh, "a/f.txt", "F");
        assert(zip_close(zh) == 0);
    }

    ZipArchive z("test.zip");
    z.open(ZipArchive::Write);
    ZipEntry dir = z.getEntry("a/");
    assert(z.renameEntry(dir, "a/b/") == 2);
    assert(z.hasEntry("a/") && z.hasEntry("a/b/") && z.hasEntry("a/b/f.txt"));

    //a file placed in the auto-recreated a/ is not part of the moved directory
    assert(z.addData("a/late.txt", "LATE", 4));
    //a file added under the moved directory after the move IS part of it
    assert(z.addData("a/b/inside.txt", "IN", 2));

    assert(z.deleteEntry(dir) == 3); //a/b/, a/b/f.txt, a/b/inside.txt
    assert(!z.hasEntry("a/b/") && !z.hasEntry("a/b/f.txt") && !z.hasEntry("a/b/inside.txt"));
    assert(z.hasEntry("a/") && z.hasEntry("a/late.txt"));
    assert(z.deleteEntry(dir) == LIBZIPPP_ERROR_INVALID_ENTRY);
    assert(z.close() == LIBZIPPP_OK);

    ZipArchive r("test.zip");
    r.open(ZipArchive::ReadOnly);
    assert(r.hasEntry("a/") && r.hasEntry("a/late.txt"));
    assert(!r.hasEntry("a/b/"));
    r.close();
    r.unlink();

    cout << " done." << endl;
}

int main() {
    test1();  test2();  test3();  test4();  test5();
    test6();  test7();  test8();  test9();  test10();
    test11(); test12(); test13(); test14(); test15();
    test16(); test17(); test18(); test19(); test20();
    test21(); test22(); test23(); test23_2(); test24();
    test25(); test26(); test27(); test28(); test29();
    test30(); test31(); test32(); test33(); test34();
    test35(); test36(); test37(); test38();
    return 0;
}


