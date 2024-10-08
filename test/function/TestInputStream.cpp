/********************************************************************
 * Copyright (c) 2013 - 2014, Pivotal Inc.
 * All rights reserved.
 *
 * Author: Zhanwei Wang
 ********************************************************************/
/********************************************************************
 * 2014 -
 * open source under Apache License Version 2.0
 ********************************************************************/
/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "client/FileSystem.h"
#include "client/InputStream.h"
#include "client/OutputStream.h"
#include "client/hdfs.h"
#include "DateTime.h"
#include "Exception.h"
#include "ExceptionInternal.h"
#include "gtest/gtest.h"
#include "Logger.h"
#include "Memory.h"
#include "TestUtil.h"
#include "Thread.h"
#include "XmlConfig.h"

#include <inttypes.h>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <cstdlib>
#include <vector>

using namespace Hdfs;
using namespace Internal;

#ifndef TEST_HDFS_PREFIX
#define TEST_HDFS_PREFIX "./"
#endif

#define BASE_DIR TEST_HDFS_PREFIX"/testInputStream/"

class TestInputStream: public ::testing::Test {
public:
    TestInputStream() :
        conf("function-test.xml") {
        conf.set("output.default.packetsize", 1024);
        fs = new FileSystem(conf);
        fs->connect();
        superfs = new FileSystem(conf);
        superfs->connect(conf.getString("dfs.default.uri"), HDFS_SUPERUSER, NULL);
        conf.set("dfs.client.read.shortcircuit", false);
        remotefs = new FileSystem(conf);
        remotefs->connect();
        superfs->setWorkingDirectory(fs->getWorkingDirectory().c_str());

        try {
            superfs->deletePath(BASE_DIR, true);
        } catch (...) {
        }

        superfs->mkdirs(BASE_DIR, 0755);
        superfs->setOwner(TEST_HDFS_PREFIX, USER, NULL);
        superfs->setOwner(BASE_DIR, USER, NULL);

        setenv("LIBHDFS3_CONF", "function-test.xml", 1);
        struct hdfsBuilder * bld = hdfsNewBuilder();
        assert(bld != nullptr);
        hdfsBuilderSetNameNode(bld, "default");
        dfs = hdfsBuilderConnect(bld);
        assert(dfs != nullptr);

        char buffer1[10], buffer2[20 * 2048];
        FillBuffer(buffer1, sizeof(buffer1), 0);
        FillBuffer(buffer2, sizeof(buffer2), 0);
        hdfsFile wFile = hdfsOpenFile(dfs, BASE_DIR"smallfile", O_WRONLY, 2048, 0, 0);
        assert(wFile != nullptr);
        
        hdfsWrite(dfs, wFile, buffer1, sizeof(buffer1));
        hdfsCloseFile(dfs, wFile);

        wFile = hdfsOpenFile(dfs, BASE_DIR"largefile", O_WRONLY, 2048, 0, 0);
        assert(wFile != nullptr);
        
        hdfsWrite(dfs, wFile, buffer2, sizeof(buffer2));
        hdfsCloseFile(dfs, wFile);
    }

    ~TestInputStream() {
        try {
            superfs->deletePath(BASE_DIR, true);
        } catch (...) {
        }

        fs->disconnect();
        delete fs;
        superfs->disconnect();
        delete superfs;
        remotefs->disconnect();
        delete remotefs;
        hdfsDisconnect(dfs);
    }

    void OpenFailed(FileSystem & tfs) {
        ASSERT_THROW(ins.open(tfs, "", true), InvalidParameter);
        FileSystem fs1(conf);
        ASSERT_THROW(ins.open(fs1, BASE_DIR"smallfile", true), HdfsIOException);
        ASSERT_NO_THROW(ins.open(tfs, BASE_DIR"smallfile", true));
    }

    void OpenforRead(FileSystem & tfs) {
        EXPECT_THROW(ins.open(tfs, BASE_DIR"a", true), FileNotFoundException);
        char buff[100];
        ASSERT_THROW(ins.read(buff, 100), HdfsIOException);
        ins.close();
    }
    void Read(FileSystem & tfs, size_t size) {
        char buff[2048];
        ins.open(tfs, BASE_DIR"largefile", true);
        ASSERT_NO_THROW(ins.read(buff, size));
        EXPECT_TRUE(CheckBuffer(buff, size, 0));
        ASSERT_NO_THROW(ins.seek(0));
        ASSERT_NO_THROW(ins.read(buff, size - 100));
        EXPECT_TRUE(CheckBuffer(buff, size - 100, 0));

        if (size == 2048) {
            ASSERT_NO_THROW(ins.seek(0));
            ASSERT_NO_THROW(ins.read(buff, size));
            EXPECT_TRUE(CheckBuffer(buff, size, 0));
            ASSERT_NO_THROW(ins.seek(2));
            ASSERT_NO_THROW(ins.read(buff, 100));
            EXPECT_TRUE(CheckBuffer(buff, 100, 2));
        } else {
            ASSERT_NO_THROW(ins.seek(0));
            ASSERT_NO_THROW(ins.read(buff, size + 100));
            EXPECT_TRUE(CheckBuffer(buff, size + 100, 0));
        }

        ins.close();
    }
    void Seek(FileSystem & tfs) {
        ins.open(tfs, BASE_DIR"smallfile", true);
        char buff[1024];
        ASSERT_NO_THROW(ins.read(buff, 100));
        ASSERT_THROW(ins.read(buff, 1024), HdfsEndOfStream);
        ASSERT_NO_THROW(ins.seek(0));
        ASSERT_NO_THROW(ins.read(buff, 100));
        ins.close();
        ins.open(tfs, BASE_DIR"smallfile", true);
        ASSERT_NO_THROW(ins.read(buff, 100));
        ASSERT_NO_THROW(ins.seek(0));
        ASSERT_NO_THROW(ins.read(buff, 100));
        ASSERT_NO_THROW(ins.seek(0));
        ASSERT_NO_THROW(ins.seek(10));
        ASSERT_THROW(ins.read(buff, 1), HdfsEndOfStream);
        ins.close();
        ASSERT_THROW(ins.seek(12), HdfsIOException);
        ins.open(tfs, BASE_DIR"smallfile", true);
        ASSERT_THROW(ins.seek(12), HdfsIOException);
        ins.close();
        ins.open(tfs, BASE_DIR"largefile", true);
        ASSERT_NO_THROW(ins.seek(1027));
        ASSERT_NO_THROW(ins.read(buff, 100));
        ins.close();
    }

    void CheckSum(FileSystem & tfs) {
        ins.open(tfs, BASE_DIR"largefile", false);
        std::vector<char> buff(10240);
        ASSERT_NO_THROW(ins.read(&buff[0], 512));
        ASSERT_NO_THROW(ins.seek(0));
        ASSERT_NO_THROW(ins.read(&buff[0], 1049));
        ins.close();
        ins.open(tfs, BASE_DIR"smallfile", false);
        ASSERT_THROW(ins.seek(13), HdfsIOException);
        ins.close();
    }

    void ReadFully(FileSystem & tfs, size_t size) {
        ins.open(tfs, BASE_DIR"largefile", false);
        char buff[20 * 2048 + 1];
        ASSERT_NO_THROW(ins.readFully(buff, size));
        EXPECT_TRUE(CheckBuffer(buff, size, 0));
        ASSERT_NO_THROW(ins.seek(0));
        ASSERT_NO_THROW(ins.readFully(buff, size - 100));
        EXPECT_TRUE(CheckBuffer(buff, size - 100, 0));
        ASSERT_NO_THROW(ins.seek(0));
        ASSERT_NO_THROW(ins.readFully(buff, size + 100));
        EXPECT_TRUE(CheckBuffer(buff, size + 100, 0));
        ASSERT_THROW(ins.readFully(buff, 20 * 2048 + 1), HdfsIOException);
        ins.close();
    }

protected:
    Config conf;
    FileSystem * fs;
    FileSystem * superfs;
    FileSystem * remotefs;  //test remote block reader
    InputStream ins;
    OutputStream ous;
    hdfsFS dfs;
};

TEST_F(TestInputStream, TestInputStream_OpenFailed) {
    OpenFailed(*fs);
    OpenFailed(*remotefs);
}

TEST_F(TestInputStream, TestInputStream_OpenforRead) {
    OpenforRead(*fs);
    OpenforRead(*remotefs);
}

TEST_F(TestInputStream, TestInputStream_Read) {
    Read(*fs, 512);
    Read(*fs, 1024);
    Read(*fs, 2048);
    Read(*remotefs, 512);
    Read(*remotefs, 1024);
    Read(*remotefs, 2048);
}
TEST_F(TestInputStream, TestInputStream_Seek) {
    Seek(*fs);
    Seek(*remotefs);
}

TEST_F(TestInputStream, TestInputStream_CheckSum) {
    CheckSum(*fs);
    CheckSum(*remotefs);
}

TEST_F(TestInputStream, TestInputStream_ReadFully) {
    ReadFully(*fs, 512);
    ReadFully(*fs, 1024);
    ReadFully(*fs, 2048);
    ReadFully(*remotefs, 512);
    ReadFully(*remotefs, 1024);
    ReadFully(*remotefs, 2048);
}

static void CheckFileContent(FileSystem * fs, std::string path, int64_t len, size_t offset) {
    InputStream in;
    EXPECT_NO_THROW(in.open(*fs, path.c_str(), true));
    std::vector<char> buff(20 * 1024 + 1);
    int rc, todo = len, batch;

    while (todo > 0) {
        batch = todo < static_cast<int>(buff.size()) ? todo : buff.size();
        batch = in.read(&buff[0], batch);
        ASSERT_TRUE(batch > 0);
        todo = todo - batch;
        rc = Hdfs::CheckBuffer(&buff[0], batch, offset);
        offset += batch;
        EXPECT_TRUE(rc);
    }

    EXPECT_NO_THROW(in.close());
}

static void WriteFile(hdfsFS dfs, std::string filename, int64_t writeSize, int flag) {
    std::vector<char> buffer(64 * 1024);
    int64_t todo, batch;
    size_t offset = 0;
    todo = writeSize;
    hdfsFile wFile = hdfsOpenFile(dfs, filename.c_str(), flag, 2048, 1, 1024 * 1024);
    ASSERT_TRUE(wFile != nullptr);

    while (todo > 0) {
        batch = todo < static_cast<int>(buffer.size()) ? todo : buffer.size();
        Hdfs::FillBuffer(&buffer[0], batch, offset);
        ASSERT_NO_THROW(DebugException(hdfsWrite(dfs, wFile, &buffer[0], batch)));
        todo -= batch;
        offset += batch;
    }

    ASSERT_NO_THROW(hdfsCloseFile(dfs, wFile););
}

static void NothrowCheckFileContent(FileSystem * fs, std::string path,
                                    int64_t len, size_t offset) {
    EXPECT_NO_THROW(CheckFileContent(fs, path, len, offset));
}

TEST_F(TestInputStream, TestReadOneFileSameTime) {
    int flag = O_WRONLY;
    int64_t readSize = 1 * 1024 * 1024 + 234;
    int64_t writeSize = 1 * 1024 * 1024 * 1024 + 234;
    std::string filename(BASE_DIR"testReadOneFileSameTime");
    std::vector<shared_ptr<thread> > threads;
    WriteFile(dfs, filename, writeSize, flag);

    for (int i = 1; i <= 50; ++i) {
        threads.push_back(
            shared_ptr<thread>(
                new thread(NothrowCheckFileContent, fs, filename, readSize, 0)));
    }

    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i]->join();
    }
}

/**
 * test read many files in the same time
 */
TEST_F(TestInputStream, TestReadManyFileSameTime) {
    int flag = O_WRONLY;
    int64_t readSize = 1 * 1024 * 1024 + 234;
    int64_t writeSize = 20 * 1024 * 1024 + 234;
    std::vector<shared_ptr<thread> > threads;
    const char * filename = BASE_DIR"testReadSameTime";

    for (int i = 1; i <= 50; ++i) {
        std::stringstream ss;
        ss.imbue(std::locale::classic());
        ss << filename << i;
        WriteFile(dfs, ss.str(), writeSize, flag);
        threads.push_back(
            shared_ptr<thread>(
                new thread(NothrowCheckFileContent, fs, ss.str(), readSize, 0)));
    }

    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i]->join();
    }
}

void static SetupTestEnv(FileSystem & fs, Config & conf) {
    FileSystem superfs(conf);
    superfs.connect(conf.getString("dfs.default.uri"), HDFS_SUPERUSER, NULL);
    superfs.setWorkingDirectory(fs.getWorkingDirectory().c_str());

    try {
        superfs.deletePath(BASE_DIR, true);
    } catch (...) {
    }

    superfs.mkdirs(BASE_DIR, 0755);
    superfs.setOwner(TEST_HDFS_PREFIX, USER, NULL);
    superfs.setOwner(BASE_DIR, USER, NULL);
    superfs.disconnect();
}

TEST(TestInputStreamWithOutputStream, TestOpenFirstAndAppend) {
    Config conf("function-test.xml");
    conf.set("dfs.client.read.shortcircuit", false);
    conf.set("input.notretry-another-node", true);
    FileSystem fs(conf);
    fs.connect();
    SetupTestEnv(fs, conf);

    setenv("LIBHDFS3_CONF", "function-test.xml", 1);
    struct hdfsBuilder * bld = hdfsNewBuilder();
    assert(bld != nullptr);
    hdfsBuilderSetNameNode(bld, "default");
    hdfsFS dfs = hdfsBuilderConnect(bld);
    assert(dfs != nullptr);

    //int prefix = 45013;
    int step = 16384;
    int prefix = 400;
    int suffix = 16034;
    int fileSize = prefix + step * 3 + suffix;
    std::vector<char> buffer;
    buffer.resize(fileSize);
    FillBuffer(&buffer[0], buffer.size(), 0);
    hdfsFile wFile = nullptr;
    ASSERT_NO_THROW(wFile = hdfsOpenFile(dfs, BASE_DIR"testOpenFirstAndAppend", O_CREAT, 2048, 1, 0));
    ASSERT_TRUE(wFile != nullptr);
    ASSERT_NO_THROW(hdfsWrite(dfs, wFile, &buffer[0], buffer.size()));
    ASSERT_NO_THROW(hdfsSync(dfs, wFile));
    ASSERT_NO_THROW(hdfsCloseFile(dfs, wFile););
    InputStream is;
    ASSERT_NO_THROW(is.open(fs, BASE_DIR"testOpenFirstAndAppend", true));
    ASSERT_NO_THROW(is.seek(prefix));
    buffer.resize(step);
    int todo = fileSize - prefix;
    size_t offset = prefix;

    while (todo > 0) {
        int batch = step;
        batch = batch < todo ? batch : todo;
        ASSERT_NO_THROW(batch = is.read(&buffer[0], batch));
        ASSERT_TRUE(CheckBuffer(&buffer[0], batch, offset));
        todo -= batch;
        offset += batch;
    }

    ASSERT_NO_THROW(is.close());
}

static double CalculateThroughput(int64_t elapsed, int64_t size) {
    return size / 1024.0 * 1000.0 / 1024.0 / elapsed;
}

TEST(TestThroughput, Throughput) {
    Config conf("function-test.xml");
    FileSystem fs(conf);
    fs.connect();
    SetupTestEnv(fs, conf);

    setenv("LIBHDFS3_CONF", "function-test.xml", 1);
    struct hdfsBuilder * bld = hdfsNewBuilder();
    assert(bld != nullptr);
    hdfsBuilderSetNameNode(bld, "default");
    hdfsFS dfs = hdfsBuilderConnect(bld);
    assert(dfs != nullptr);

    const char * filename = BASE_DIR"TestThroughput";
    //const char * filename = "TestThroughput_SeekAhead";
    std::vector<char> buffer(64 * 1024);
    int64_t fileLength = 5 * 1024 * 1024 * 1024ll;
    int64_t todo = fileLength, batch, elapsed;
    size_t offset = 0;
    steady_clock::time_point start, stop;

    if (!fs.exist(filename)) {
        hdfsFile wFile = nullptr;
        start = steady_clock::now();
        EXPECT_NO_THROW(
            DebugException(wFile = hdfsOpenFile(dfs, filename, O_WRONLY, 2048, 0, 0)));
        ASSERT_TRUE(wFile != nullptr);

        while (todo > 0) {
            batch = todo < static_cast<int>(buffer.size()) ?
                    todo : buffer.size();
            ASSERT_NO_THROW(DebugException(hdfsWrite(dfs, wFile, &buffer[0], batch)));
            todo -= batch;
            offset += batch;
        }

        ASSERT_NO_THROW(DebugException(hdfsCloseFile(dfs, wFile)));
        steady_clock::time_point stop = steady_clock::now();
        elapsed = ToMilliSeconds(start, stop);
        LOG(INFO, "write file time %" PRId64 " ms, throughput is %lf mbyte/s",
            elapsed, CalculateThroughput(elapsed, fileLength));
    }

    start = steady_clock::now();
    InputStream in;
    EXPECT_NO_THROW(in.open(fs, filename, true));
    std::vector<char> buff(20 * 1024 + 1);
    todo = fileLength;

    while (todo > 0) {
        batch = todo < static_cast<int>(buff.size()) ? todo : buff.size();
        batch = in.read(&buff[0], batch);
        ASSERT_TRUE(batch > 0);
        todo = todo - batch;
        offset += batch;
    }

    EXPECT_NO_THROW(in.close());
    stop = steady_clock::now();
    elapsed = ToMilliSeconds(start, stop);
    LOG(INFO, "read file time %" PRId64 " ms, throughput is %lf mbyte/s", elapsed, CalculateThroughput(elapsed, fileLength));
    fs.deletePath(filename, true);
}

TEST(TestThroughput, TestSeekAhead) {
    Config conf("function-test.xml");
    conf.set("dfs.client.read.shortcircuit", true);
    FileSystem fs(conf);
    fs.connect();
    SetupTestEnv(fs, conf);

    setenv("LIBHDFS3_CONF", "function-test.xml", 1);
    struct hdfsBuilder * bld = hdfsNewBuilder();
    assert(bld != nullptr);
    hdfsBuilderSetNameNode(bld, "default");
    hdfsFS dfs = hdfsBuilderConnect(bld);
    assert(dfs != nullptr);

    int64_t offset = 0;
    int64_t fileLength = 20 * 1024 * 1024 * 1024ll;
    int64_t todo = fileLength, batch, elapsed;
    const char * filename = BASE_DIR"TestThroughput_SeekAhead";
    //const char * filename = "TestThroughput_SeekAhead";
    steady_clock::time_point start, stop;

    if (!fs.exist(filename)) {
        std::vector<char> buffer(64 * 1024);
        hdfsFile wFile = nullptr;
        start = steady_clock::now();
        EXPECT_NO_THROW(
            DebugException(wFile = hdfsOpenFile(dfs, filename, O_WRONLY, 2048, 0, 0)));
        ASSERT_TRUE(wFile != nullptr);

        while (todo > 0) {
            batch = todo < static_cast<int>(buffer.size()) ?
                    todo : buffer.size();
            ASSERT_NO_THROW(DebugException(hdfsWrite(dfs, wFile, &buffer[0], batch)));
            todo -= batch;
        }

        ASSERT_NO_THROW(DebugException(hdfsCloseFile(dfs, wFile)));
        stop = steady_clock::now();
        elapsed = ToMilliSeconds(start, stop);
        LOG(INFO, "write file time %" PRId64 " ms, throughput is %lf mbyte/s",
            elapsed, CalculateThroughput(elapsed, fileLength));
    }

    start = steady_clock::now();
    InputStream in;
    EXPECT_NO_THROW(in.open(fs, filename, true));
    std::vector<char> buff(8 * 1024 * 1024 + 1);
    todo = fileLength;

    while (todo > 0) {
        batch = todo < static_cast<int>(buff.size()) ? todo : buff.size();
        DebugException(in.readFully(&buff[0], batch / 8));
        //seek
        in.seek(offset + batch);
        todo = todo - batch;
        offset += batch;
    }

    EXPECT_NO_THROW(in.close());
    stop = steady_clock::now();
    elapsed = ToMilliSeconds(start, stop);
    LOG(INFO, "read and seek file time %" PRId64 " ms, throughput is %lf mbyte/s",
        elapsed, CalculateThroughput(elapsed, fileLength));
    fs.deletePath(filename, true);
}

TEST(TestThroughput, TestSeek) {
    setenv("LIBHDFS3_CONF", "function-test.xml", 1);
    struct hdfsBuilder * bld = hdfsNewBuilder();
    assert(bld != nullptr);
    hdfsBuilderSetNameNode(bld, "default");
    hdfsFS dfs = hdfsBuilderConnect(bld);
    assert(dfs != nullptr);

    const char * filename = BASE_DIR"TestThroughput_TestSeek";
    hdfsFile wFile = hdfsOpenFile(dfs, filename, O_WRONLY, 2048, 0, 0);
    ASSERT_TRUE(nullptr != wFile);

    int64_t fileLength = 256 * 1024 * 1024ll;
    int64_t todo = fileLength, batch;
    int64_t written = 0;
    int32_t mod = 31;
    std::vector<char> writeBuffer(1024 * 1024);

    while (todo > 0) {
        int64_t val = (fileLength - todo) % mod;
        for (int32_t i = 0; i < static_cast<int>(writeBuffer.size()); ++i) {
            writeBuffer[i] = val % mod;
            val = (val + 1) % mod;
        }
        batch = todo < static_cast<int>(writeBuffer.size()) ? todo : writeBuffer.size();
        ASSERT_NO_THROW(DebugException(written = hdfsWrite(dfs, wFile, &writeBuffer[0], batch)));
        todo -= written;
    }
    ASSERT_TRUE(0 == hdfsCloseFile(dfs, wFile));

    srand((int)time(0));
    steady_clock::time_point start, stop;
    hdfsFile rFile = nullptr;
    int32_t times = 100;
    int64_t blockSize = 64 * 1024 * 1024;
    int64_t totalElapsed = 0;

    for(int32_t i = 0; i < times; ++i) {
        int64_t pos = rand() % blockSize;
        int64_t readSize = rand() % (blockSize - pos) + 1;
        std::vector<char> readBuffer(readSize);

        rFile = hdfsOpenFile(dfs, filename, O_RDONLY, 2048, 0, 0);
        ASSERT_TRUE(nullptr != rFile);
        int32_t read = hdfsRead(dfs, rFile, &readBuffer[0], 1);
        ASSERT_TRUE(read == 1);

        start = steady_clock::now();
        int32_t seekResult = hdfsSeek(dfs, rFile, pos);
        read = hdfsRead(dfs, rFile, &readBuffer[0], readBuffer.size());
        stop = steady_clock::now();
        totalElapsed += ToMilliSeconds(start, stop);

        ASSERT_TRUE(readBuffer[0] == pos % mod);
        ASSERT_TRUE(readBuffer[read - 1] == (pos + read - 1) % mod);
        ASSERT_TRUE(0 == seekResult);
        ASSERT_TRUE(0 == hdfsCloseFile(dfs, rFile));
    }

    double avgElapsed = static_cast<double>(totalElapsed) / times;
    printf("seek and read file time %lf ms\n", avgElapsed);
   
}
