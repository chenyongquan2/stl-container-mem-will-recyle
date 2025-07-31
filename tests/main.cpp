#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <spdlog/spdlog.h>
#include <spdlog/async.h>

int main(int argc, char** argv)
{
	// // initialize logging system
    // spdlog::cfg::load_env_levels();
    // spdlog::flush_on(spdlog::level::err);
    // spdlog::flush_every(std::chrono::seconds(3));
    // spdlog::set_default_logger(gain_logger("test"));

	SPDLOG_INFO("start test/main()");

    //如果需要用vs来附件调试，可以放开这里。
    //Sleep(20000);

    ::testing::InitGoogleMock(&argc, argv);
    int res = RUN_ALL_TESTS();
    return res;
}