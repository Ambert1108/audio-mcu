#include "Config.h"
#include "HttpProcessUnit.h"

#include <iostream>

int main(int argc, char* argv[]) {
	using config = seeker::IniConfig;
	using namespace aom;
	std::string profile;
	if (argc == 2) profile = argv[1];
	else {
		std::cout << "no application file, use default file" << std::endl;
		profile = "resources/config.ini";
	}
	std::cout << "compass is Loading" << std::endl;
	std::cout << "Reading config file:" << profile << std::endl;
	MediaControlUnit* mcu = nullptr;
	try {
		config::init(profile);
		std::string configFileString = seeker::file::readFileAsString(profile);
		
		std::string logPattern = "%^[%d %H:%M:%S.%e %s:%#] [%L]:%$ %v";
		std::string logFilename = config::Get("log", "log_name", "applicaiton.log");
		bool openStdOut = config::GetBoolean("log", "std_out", true);
		int logLevel = config::GetInteger("log", "log_level", 2);
		if (logLevel != 2) define_spdlog_level(logLevel);
		
		I_LOG("config file contants:{} \n", configFileString);
		std::cout << std::endl;
		
		std::cout << "Hello User, mem:" << seeker::file::getVmRSS() << "KB" << std::endl;
		seeker::Logger::init(logFilename, false, openStdOut, true, logPattern, logLevel);
		
		T_LOG("TRACE LEVEL IS OPEN");
		D_LOG("DEBUG LEVEL IS OPEN");
		
		I_LOG("//////////////////////////////////");
		I_LOG("//                              //");
		I_LOG("//                              //");
		I_LOG("//        Hello User            //");
		I_LOG("//      Faust version:{}     //", version);
		I_LOG("//                              //");
		I_LOG("//                              //");
		I_LOG("//////////////////////////////////");
		
		std::string httpIp = config::Get("main", "ip", "0.0.0.0");
		port_t httpPort = config::GetInteger("main", "port", 50012);
		if (httpIp.empty()) {
			E_LOG("[boot::Error] httpServer.host=[{}]", httpIp);
			exit(-1);
		}
		
		if (httpPort < 1024) {
			E_LOG("[boot::Error] httpServer.binding_port=[{}]", httpPort);
			exit(-1);
		}

		mcu = MediaControlUnit::getInstance();
		if (mcu->init() != 0) {
			E_LOG("[boot::Error] Init Media Control Unit failed");
			throw std::logic_error("Init Media Control Unit failed");
		}
		
		std::unique_ptr<HttpProcessUnit> hpu = std::make_unique<HttpProcessUnit>(httpIp, httpPort);
		
		hpu->open();
		hpu.reset();
		if (!MediaControlUnit::own()) {
			throw std::runtime_error("get error: JobManager is not own!");
		}
		delete mcu;
		mcu = nullptr;
		seeker::Logger::shutdown();
		std::this_thread::sleep_for(std::chrono::seconds(1));
		std::cout << "Goodbye User, mem:" << seeker::file::getVmRSS() << "KB" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	catch (std::exception& ex) {
		E_LOG("[boot::Error] get exception in {}", ex.what());
		delete mcu;
		mcu = nullptr;
		seeker::Logger::shutdown();
	}
	catch (int err) {
		E_LOG("[boot::Error] Http Process Unit listening error:{}, check http ip and port are legal", err);
		delete mcu;
		mcu = nullptr;
		seeker::Logger::shutdown();
	}

	return 0;
}