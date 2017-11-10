// Copyright (c) 2016 Klemens D. Morgenstern
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)


#define BOOST_TEST_MAIN


#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/config.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/process/async_pipe.hpp>
#include <boost/process/pipe.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

using namespace std;
namespace bp = boost::process;
namespace asio = boost::asio;
namespace bfs = boost::filesystem;

BOOST_AUTO_TEST_CASE(plain_async, *boost::unit_test::timeout(5))
{
    asio::io_context ios;
    bp::async_pipe pipe{ios};

    std::string st = "test-string\n";

    asio::streambuf buf;

    asio::async_write(pipe, asio::buffer(st), [](const boost::system::error_code &, std::size_t){});
    asio::async_read_until(pipe, buf, '\n', [](const boost::system::error_code &, std::size_t){});

    ios.run();

    std::string line;
    std::istream istr(&buf);
    BOOST_CHECK(std::getline(istr, line));

    line.resize(11);
    BOOST_CHECK_EQUAL(line, "test-string");

}

BOOST_AUTO_TEST_CASE(closed_transform)
{
    asio::io_context ios;

    bp::async_pipe ap{ios};

    BOOST_CHECK(ap.is_open());
    bp::pipe p2 = static_cast<bp::pipe>(ap);
    BOOST_CHECK(p2.is_open());

    ap.close();
    BOOST_CHECK(!ap.is_open());

    bp::pipe p  = static_cast<bp::pipe>(ap);
    BOOST_CHECK(!p.is_open());

}

BOOST_AUTO_TEST_CASE(multithreaded_async_pipe)
{
    asio::io_context ioc;

    std::vector<std::thread> threads;
    for (int i = 0; i < std::thread::hardware_concurrency(); i++)
    {
        threads.emplace_back([&ioc]
        {
            std::vector<bp::async_pipe*> pipes;
            for (size_t i = 0; i < 100; i++)
                pipes.push_back(new bp::async_pipe(ioc));
            for (auto &p : pipes)
                delete p;
        });
    }
    for (auto &t : threads)
        t.join();
}

namespace
{
struct named_pipe_test_fixture
{
    using async_pipe_ptr = std::shared_ptr<bp::async_pipe>;

    asio::io_context ioc;

    boost::uuids::random_generator uuidGenerator;
    const bfs::path pipe_path;
    const std::string pipe_name;

    async_pipe_ptr created_pipe;
    async_pipe_ptr opened_pipe;

    const char delim;
    const std::string st_base;
    const std::string st;

    asio::streambuf buf;

    named_pipe_test_fixture()
    : ioc{}
    // generate a unique random path/name for for the pipe
    , uuidGenerator{}
    , pipe_path{
        #if defined(BOOST_POSIX_API)
            bfs::temp_directory_path()
        #elif defined(BOOST_WINDOWS_API)
            bfs::path("\\\\.\\pipe")
        #endif
        / boost::uuids::to_string(uuidGenerator())
      }
    , pipe_name{pipe_path.string()}
    , delim{'\n'}
    , st_base{"test-string"}
    , st{st_base + delim}
    , buf{}
    {
        // create and open the pipe "file"
        created_pipe.reset(new bp::async_pipe(ioc, pipe_name));
        BOOST_CHECK(created_pipe->is_open());

        // open the existing pipe
        opened_pipe.reset(new bp::async_pipe(ioc, pipe_name, true));
        BOOST_CHECK(opened_pipe->is_open());
    }

    ~named_pipe_test_fixture()
    {
        std::string line;
        std::istream istr(&buf);
        BOOST_CHECK(std::getline(istr, line));

        BOOST_CHECK_EQUAL(line, st_base);

        // close pipes
        created_pipe->close();
        BOOST_CHECK(!created_pipe->is_open());
        opened_pipe->close();
        BOOST_CHECK(!opened_pipe->is_open());
        // cleanup
        bfs::remove(pipe_path);
        BOOST_CHECK(!bfs::exists(pipe_path));
    }

    #define log_stmt(stmt) { \
            BOOST_TEST_MESSAGE(__LINE__ << ": " << #stmt); \
            stmt; \
            BOOST_TEST_MESSAGE(__LINE__ << ": done"); \
        }

    static
    void test_plain_async(asio::io_context& ioc,
                          async_pipe_ptr async_writer, async_pipe_ptr async_reader,
                          const std::string& st, asio::streambuf& buf, const char delim)
    {
        log_stmt(
            asio::async_write(*async_writer, asio::buffer(st),
                [](const boost::system::error_code &, std::size_t){
                    BOOST_TEST_MESSAGE("        in async_write");
                })
        );
        log_stmt(
            asio::async_read_until(*async_reader, buf, delim,
                [](const boost::system::error_code &, std::size_t){
                    BOOST_TEST_MESSAGE("        in async_read_until");
                })
        );
        log_stmt(ioc.run());
    }

    #undef log_stmt
};

}

BOOST_FIXTURE_TEST_SUITE(existing_named_pipe_plain_async, named_pipe_test_fixture)

BOOST_AUTO_TEST_CASE(existing_named_pipe_plain_async_created_pipe, *boost::unit_test::timeout(5))
{
    test_plain_async(ioc, created_pipe, created_pipe, st, buf, delim);
}

BOOST_AUTO_TEST_CASE(existing_named_pipe_plain_async_opened_pipe, *boost::unit_test::timeout(5))
{
    test_plain_async(ioc, opened_pipe, opened_pipe, st, buf, delim);
}

// hangs indefinitely on windows
#if !defined(BOOST_WINDOWS_API)

BOOST_AUTO_TEST_CASE(existing_named_pipe_plain_async_created_to_opened_pipe, *boost::unit_test::timeout(5))
{
    test_plain_async(ioc, created_pipe, opened_pipe, st, buf, delim);
}

BOOST_AUTO_TEST_CASE(existing_named_pipe_plain_async_opened_to_created_pipe, *boost::unit_test::timeout(5))
{
    test_plain_async(ioc, opened_pipe, created_pipe, st, buf, delim);
}

#endif

BOOST_AUTO_TEST_SUITE_END()
