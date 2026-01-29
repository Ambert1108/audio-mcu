#include "HttpProcessUnit.h"

namespace aom {
	inline std::string reqInfoDetail(const Request& req) {
		std::stringstream ss;
		ss << "reqInfo:";
		ss << "path=" << req.path << ";";
		ss << "method=" << req.method << ";";
		ss << "params=[";
		for (auto& h : req.params) {
			ss << h.first << ":" << h.second << ",";
		}
		ss << "];";

		ss << "headers=[";
		for (auto& h : req.headers) {
			ss << h.first << ":" << h.second << ",";
		}
		ss << "];";
		ss << "body=" << req.body << ";";

		return ss.str();
	}

	inline std::string simpleReqInfo(const Request& req) {
		std::stringstream ss;
		ss << "reqInfo:";
		ss << "method=" << req.method << ";";
		ss << "path=" << req.path << ";";
		ss << "params=[";
		for (auto& h : req.params) {
			ss << "{" << h.first << ":" << h.second << "}";
		}
		ss << "];";
		return ss.str();
	}

	inline std::string reqInfo(const Request& req) {
		std::stringstream ss;
		ss << "reqInfo:";
		ss << "method=" << req.method << ";";
		ss << "path=" << req.path << ";";
		ss << "params=[";
		for (auto& h : req.params) {
			ss << "{" << h.first << ":" << h.second << "}";
		}
		ss << "];";
		ss << "body=" << req.body;
		return ss.str();
	}

	inline std::string respInfo(const Response& resp) {
		const static std::string newlineRegString = R"regex(\r?\n[ ]*)regex";
		const static std::regex newlineReg(newlineRegString);
		std::string info = fmt::format("respInfo: status={}; reason={}; body={}", resp.status,
			resp.reason, resp.body);
		info = std::regex_replace(info, newlineReg, "");
		return info;
	}

	inline void showReq(const Request& req) {
		std::string jobId = {};
		std::string businessType = {};
		json j = {};
		try {
			if (req.has_param("jobId")) {
				jobId = req.get_param_value("jobId");
			}else { jobId = "Unknown"; }
			if (req.has_param("businessType")) {
				businessType = req.get_param_value("businessType");
			}
			else { businessType = "00"; }
		}
		catch (std::exception& ex) {
			D_LOG("get exception:{} in {}", ex.what(), __LINE__);
		}
		catch (...) {
			D_LOG("get unknown exception in {}", __LINE__);
		}
		if (jobId.empty()) jobId = "Unknown";
		if (businessType.empty()) businessType = "00";

		I_LOG("jobId[{}:{}] ----------------------  show Request  -----------------------", jobId, businessType);
		I_LOG("jobId[{}:{}] path    : [{}]", jobId, businessType, req.path);
		I_LOG("jobId[{}:{}] method  : [{}]", jobId, businessType, req.method);
		for (auto& h : req.params) {
			I_LOG("jobId[{}:{}] params  : [{}] : [{}]", jobId, businessType, h.first, h.second);
		}
		for (auto& h : req.headers) {
			I_LOG("jobId[{}:{}] headers : [{}] : [{}]", jobId, businessType, h.first, h.second);
		}

		I_LOG("jobId[{}:{}] body    : [{}]", jobId, businessType, req.body);
		I_LOG("--------------------------------------------------------");
	}

	void countAvg(std::atomic<uint32_t>& sum, std::atomic<uint32_t>& count, float& avg) {
		if (count < 100) return;
		avg = (float)sum / count;
		sum = 0;
		count = 0;
	}

	using namespace seeker::json;

	inline HttpTask baseTask(const std::string& taskName, const UndefineHttpTask work) {
		auto newWork = [=](const Request& req, Response& res) {
			try {
				T_LOG("Http Task access task [{}]", taskName);

				work(req, res, taskName);
				if (res.status >= 1000) {
					W_LOG("[{}] http task response status={}, req.path={}", taskName, res.status, req.path);
				}
			}
			catch (std::runtime_error& ex) {
				res.status = 1100;
				E_LOG("[{}] http task throw runtime_error: [{}], {}", taskName, ex.what(),
					reqInfoDetail(req));
			}
			catch (std::exception& ex) {
				res.status = 1100;
				E_LOG("[{}] http task throw exception: [{}], {}", taskName, ex.what(),
					reqInfoDetail(req));
			}
			catch (...) {
				res.status = 1100;
				E_LOG("[{}] http task throw unknown_error: {}", taskName, reqInfoDetail(req));
			}
			};
		return newWork;
	}

	HttpTask HttpProcessUnit::setWork(const std::string& actionName, Handle func) {
		auto work = std::bind(std::mem_fn(func), this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		return baseTask(actionName, work);
	}

	void requestSet(const Request& req, Response& rsp) {
		rsp.set_header("Access-Control-Allow-Origin", "*");
		rsp.set_header("Access-Control-Allow-Headers",
			"Content-Type,Access-Control-Allow-Headers,Authorization,X-Requested-With'");
		rsp.set_header("Access-Control-Allow-Methods", "*");
		rsp.set_header("Access-Control-Max-Age", "3600");
		rsp.set_header("Cache-Control", "no-cache");
		rsp.set_content("preflight succeed", "text/plain");
	}

	HttpTask HttpProcessUnit::setOption(UndefineHandle func) {
		auto work = std::bind(func, std::placeholders::_1, std::placeholders::_2);
		return [&](const Request& req, Response& rsp) {
			try {
				work(req, rsp);
			}
			catch (std::exception& ex) {
				E_LOG("[setOption] http task throw unknown_error: {}", reqInfoDetail(req));
			}
			};
	}

	HttpProcessUnit::HttpProcessUnit(const std::string& httpIp, const port_t& httpPort)
		: _httpIp(httpIp), _httpPort(httpPort) {
	}

	HttpProcessUnit::~HttpProcessUnit() {
		try {
			close();
		}
		catch (std::exception& ex) {
			E_LOG("[HPU::Error] destory use close() failed:{}", ex.what());
		}
	}

	void HttpProcessUnit::open() {
		serverStartTime = seeker::time::currentTime();
		startTimePoint = seeker::time::toString(serverStartTime);
		isOpen = true;
		I_LOG("Http Process Unit is loading, http listening {}:{}", _httpIp, _httpPort);
		mcu = MediaControlUnit::getInstance();
		T_LOG("manager ref count:{}", MediaControlUnit::refCount());

		hpuCheck = InvokeTimer::CreateTimer(std::chrono::seconds(hpucheckInterval), true, [&]() {
			countAvg(status.createConsumeSum, status.createConsumeCount, status.createConsumeAvg);
			countAvg(status.addConsumeSum, status.addConsumeCount, status.addConsumeAvg);
			countAvg(status.removeConsumeSum, status.removeConsumeCount, status.removeConsumeAvg);
			countAvg(status.endConsumeSum, status.endConsumeCount, status.endConsumeAvg);
			countAvg(status.openConsumeSum, status.openConsumeCount, status.openConsumeAvg);
			countAvg(status.closeConsumeSum, status.closeConsumeCount, status.closeConsumeAvg);

			I_LOG("[HPU::check] create[{}/{}/{:.3f}] add[{}/{}/{:.3f}] remove[{}/{}/{:.3f}]"
				"end[{}/{}/{:.3f}] open[{}/{}/{:.3f}] close[{}/{}/{:.3f}]",
				status.createCount, status.createOk, status.createConsumeAvg, 
				status.addCount, status.addOk, status.addConsumeAvg, 
				status.removeCount, status.removeOk, status.removeConsumeAvg,
				status.endCount, status.endOk + mcu->autoCloseCount(), status.endConsumeAvg,
				status.openCount, status.openOk, status.openConsumeAvg,
				status.closeCount, status.closeOk, status.closeConsumeAvg);
			});
		hpuCheck->Start();

		workingLoop();
		I_LOG("Http Process Unit loading finish and start listening...");
		if (!svr.listen(_httpIp, _httpPort)) throw 404;
	}

	void HttpProcessUnit::close() {
		I_LOG("Http Process Unit is closing, current mem:{}KB", seeker::file::getVmRSS());
		isOpen = false;
		if (hpuCheck) hpuCheck->Cancel();
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		
		MediaControlUnit::giveInstance(mcu);
		T_LOG("manager ref count:{}", MediaControlUnit::refCount());

		svr.stop();
	}

	void HttpProcessUnit::workingLoop() {
		using namespace seeker::json;
		I_LOG("Http Process Unit is setting up request loop...");

		svr.Post(CREATE_JOB_URL, setWork("Create Job Request", &HttpProcessUnit::createRequest));
		svr.Post(ADD_CHANNEL_URL, setWork("Add Channel Request", &HttpProcessUnit::addRequest));
		svr.Post(REMOVE_CHANNEL_URL, setWork("Remove Channel Request", &HttpProcessUnit::removeRequest));
		svr.Post(END_JOB_URL, setWork("End Job Request", &HttpProcessUnit::endRequest));
		svr.Post(OPEN_MIRCO_URL, setWork("Open Mircophone Request", &HttpProcessUnit::openRequest));
		svr.Post(CLOSE_MIRCO_URL, setWork("Close Mircophone Request", &HttpProcessUnit::closeRequest));
		svr.Get(QUERY_BASE_URL, setWork("Query info Request", &HttpProcessUnit::queryBaseRequest));
		svr.Options(QUERY_BASE_URL, setOption(&requestSet));
		svr.Get(QUERY_LIST_URL, setWork("Query List Request", &HttpProcessUnit::queryListRequest));
		svr.Options(QUERY_LIST_URL, setOption(&requestSet));

		svr.set_error_handler([](const Request& req, Response& res) {
			std::string jobId = {};
			std::string businessType = {};
			json j = {};
			try {
				j = json::parse(req.body);
				jobId = req.get_param_value("jobId");
			}
			catch (std::exception& ex) {
				D_LOG("get exception:{} in {}", ex.what(), __LINE__);
			}
			if (jobId.empty()) jobId = "unknown";
			W_LOG("jobId[{}], HttpServer got unknown error. stateCode={}", jobId, res.status);
			showReq(req);
			DefaultResponse rsp{ UNKNOWN_ERROR, GET_UNKNOWN_MSG };
			res.status = 500;
			res.set_content(toJsonString(rsp), ContentType::json);

			I_LOG("jobId[{}] -> ERROR_HANDLER resp: {}", jobId, respInfo(res));
			});


		svr.set_exception_handler([](const Request& req, Response& res, std::exception_ptr ep) {
			try {
				std::rethrow_exception(ep);
			}
			catch (std::exception& e) {
				W_LOG("[HPU::Warn] got unknown exception. stateCode={} exception={}", res.status,
					e.what());
				showReq(req);
			}
			catch (...) {
				W_LOG("[HPU::Warn] got unknown exception. stateCode={} exception unknown", res.status);
				showReq(req);
			}
			DefaultResponse resp{ GET_EXCEPTION_ERROR, GET_EXCEPTION_MSG };
			res.status = 500;
			res.set_content(toJsonString(resp), ContentType::json);
			});


		svr.set_post_routing_handler([](const Request& req, Response& res) {
			std::string jobId = {};
			std::string businessType = {};
			try {
				if (req.method == "POST") {
					try {
						jobId = req.get_param_value("jobId");
						json j = json::parse(req.body);
					}
					catch (std::exception& ex) {
						D_LOG("get exception:{}", ex.what());
					}
					if (jobId.empty()) jobId = "Unknown";
					I_LOG("jobId[{}] -> POST_ROUTING_HANDLER http resp: {}", jobId, respInfo(res));
				}
			}
			catch (std::exception& e) {
				W_LOG("[HPU::Warn] got unknown exception. stateCode={} exception={}", res.status,
					e.what());
				showReq(req);
			}
			catch (...) {
				W_LOG("[HPU::Warn] got unknown exception. stateCode={} exception unknown", res.status);
				showReq(req);
			}
			});
	}

	void HttpProcessUnit::createRequest(const Request& req, Response& rsp, const std::string& name) {
		I_LOG("[hpu::createRequest] receive request: {} -> {}", name, reqInfo(req));
		status.createCount.fetch_add(1);
		auto cTime = seeker::time::currentTime();
		if (!req.has_param("jobId")) {
			E_LOG("[hpu::createRequest] request has not param: jobId");
			DefaultResponse errRsp{ PARAM_JOBID_EMPTY, JOBID_EMPTY_MSG };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		std::string jobId = req.get_param_value("jobId");
		CreateJobContext context;
		//try {
		//	fromJsonString(context, req.body);
		//}
		//catch (std::exception& ex) { 
		//	E_LOG("[hpu::createRequest->{}] require body parse error:{}", jobId, ex.what());
		//	DefaultResponse errRsp{ PARSER_BODY_ERROR, PARSER_FAIL_MSG };
		//	rsp.set_content(toJsonString(errRsp), ContentType::json);
		//	return;
		//}
		context.jobId = jobId;
		bool ret = mcu->createMpu(context);
		if (!ret) {
			E_LOG("[hpu::createRequest->{}]  {}, {}", jobId, 100, "error");
			DefaultResponse errRsp{ 100, "error" };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}

		DefaultResponse resp;
		int64_t consumeTime = seeker::time::currentTime() - cTime;
		I_LOG("[hpu::createRequest->{}] handle use {}ms", jobId, consumeTime);
		status.createOk.fetch_add(1);
		status.createConsumeSum.fetch_add(consumeTime);
		status.createConsumeCount.fetch_add(1);
		rsp.set_content(toJsonString(resp), ContentType::json);
	}

	void HttpProcessUnit::addRequest(const Request& req, Response& rsp, const std::string& name) {
		I_LOG("[hpu::addRequest] receive request: {} -> {}", name, reqInfo(req));
		status.addCount.fetch_add(1);
		auto cTime = seeker::time::currentTime();
		if (!req.has_param("channelId")) {
			E_LOG("[hpu::addRequest] request has not param: channelId");
			DefaultResponse errRsp{ PARAM_JOBID_EMPTY, JOBID_EMPTY_MSG };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		std::string channelId = req.get_param_value("channelId");
		AddChnlContext context;
		try {
			fromJsonString(context, req.body);
		}
		catch (std::exception& ex) {
			E_LOG("[hpu::addRequest->{}:{}] require body parse error:{}", context.jobId, channelId, ex.what());
			DefaultResponse errRsp{ PARSER_BODY_ERROR, PARSER_FAIL_MSG };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		context.chnlId = channelId;
		ListenAddr addr;
		bool ret = mcu->addChnl(context, addr);
		if (!ret) {
			E_LOG("[hpu::addRequest->{}:{}] {}, {}", context.jobId, channelId, 100, "error");
			DefaultResponse errRsp{ 100, "error" };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}

		AddChnlRespInfo resp;
		resp.listenIp = addr.ip;
		resp.listenPort = addr.port;
		status.addOk.fetch_add(1);
		int64_t consumeTime = seeker::time::currentTime() - cTime;
		I_LOG("[hpu::addRequest->{}:{}] handle use {}ms", context.jobId, channelId, consumeTime);
		status.addConsumeSum.fetch_add(consumeTime);
		status.addConsumeCount.fetch_add(1);
		rsp.set_content(toJsonString(resp), ContentType::json);
	}

	void HttpProcessUnit::removeRequest(const Request& req, Response& rsp, const std::string& name) {
		I_LOG("[hpu::removeRequest] receive request: {} -> {}", name, reqInfo(req));
		status.removeCount.fetch_add(1);
		auto cTime = seeker::time::currentTime();
		if (!req.has_param("channelId")) {
			E_LOG("[HPU::Error] request has not param:channelId");
			DefaultResponse errRsp{ PARAM_JOBID_EMPTY, JOBID_EMPTY_MSG };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		std::string channelId = req.get_param_value("channelId");
		RemoveChnlContext context;
		try {
			fromJsonString(context, req.body);
		}
		catch (std::exception& ex) {
			E_LOG("[hpu::removeRequest->{}:{}] require body parse error:{}", context.jobId, channelId, ex.what());
			DefaultResponse errRsp{ PARSER_BODY_ERROR, PARSER_FAIL_MSG };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		context.chnlId = channelId;
		bool ret = mcu->removeChnl(context);
		if (!ret) {
			E_LOG("[hpu::removeRequest->{}:{}] {}->{}", context.jobId, channelId, 100, "error");
			DefaultResponse errRsp{ 100, "error" };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		DefaultResponse resp;
		status.removeOk.fetch_add(1);
		int64_t consumeTime = seeker::time::currentTime() - cTime;
		I_LOG("[hpu::removeRequest->{}:{}] handle use {}ms", context.jobId, channelId, consumeTime);
		status.removeConsumeSum.fetch_add(consumeTime);
		status.removeConsumeCount.fetch_add(1);
		rsp.set_content(toJsonString(resp), ContentType::json);
	}

	void HttpProcessUnit::openRequest(const Request& req, Response& rsp, const std::string& name) {
		I_LOG("[hpu::openRequest] receive request: {} -> {}", name, reqInfo(req));
		status.openCount.fetch_add(1);
		auto cTime = seeker::time::currentTime();
		MicCtrlContext context;
		try {
			fromJsonString(context, req.body);
		}
		catch (std::exception& ex) {
			E_LOG("[hpu::openRequest->{}] require body parse error:{}", context.jobId, ex.what());
			DefaultResponse errRsp{ PARSER_BODY_ERROR, PARSER_FAIL_MSG };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		bool ret = mcu->openMic(context);
		if (!ret) {
			E_LOG("[hpu::openRequest->{}]  {}, {}", context.jobId, 100, "error");
			DefaultResponse errRsp{ 100, "error" };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}

		DefaultResponse resp;
		int64_t consumeTime = seeker::time::currentTime() - cTime;
		I_LOG("[hpu::openRequest->{}] handle use {}ms", context.jobId, consumeTime);
		status.openOk.fetch_add(1);
		status.openConsumeSum.fetch_add(consumeTime);
		status.openConsumeCount.fetch_add(1);
		rsp.set_content(toJsonString(resp), ContentType::json);
	}

	void HttpProcessUnit::closeRequest(const Request& req, Response& rsp, const std::string& name) {
		I_LOG("[hpu::closeRequest] receive request: {} -> {}", name, reqInfo(req));
		status.closeCount.fetch_add(1);
		auto cTime = seeker::time::currentTime();
		MicCtrlContext context;
		try {
			fromJsonString(context, req.body);
		}
		catch (std::exception& ex) {
			E_LOG("[hpu::closeRequest->{}] require body parse error:{}", context.jobId, ex.what());
			DefaultResponse errRsp{ PARSER_BODY_ERROR, PARSER_FAIL_MSG };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		bool ret = mcu->closeMic(context);
		if (!ret) {
			E_LOG("[hpu::closeRequest->{}]  {}, {}", context.jobId, 100, "error");
			DefaultResponse errRsp{ 100, "error" };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}

		DefaultResponse resp;
		int64_t consumeTime = seeker::time::currentTime() - cTime;
		I_LOG("[hpu::closeRequest->{}] handle use {}ms", context.jobId, consumeTime);
		status.closeOk.fetch_add(1);
		status.closeConsumeSum.fetch_add(consumeTime);
		status.closeConsumeCount.fetch_add(1);
		rsp.set_content(toJsonString(resp), ContentType::json);
	}

	void HttpProcessUnit::endRequest(const Request& req, Response& rsp, const std::string& name) {
		I_LOG("[hpu::endRequest] receive request: {} -> {}", name, reqInfo(req));
		status.removeCount.fetch_add(1);
		auto cTime = seeker::time::currentTime();
		if (!req.has_param("jobId")) {
			E_LOG("[HPU::Error] request has not param:jobId");
			DefaultResponse errRsp{ PARAM_JOBID_EMPTY, JOBID_EMPTY_MSG };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		std::string jobId = req.get_param_value("jobId");
		bool ret = mcu->endMpu(jobId);
		if (!ret) {
			E_LOG("[hpu::endRequest->{}] {}->{}", jobId, 100, "error");
			DefaultResponse errRsp{ 100, "error" };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		DefaultResponse resp;
		status.removeOk.fetch_add(1);
		int64_t consumeTime = seeker::time::currentTime() - cTime;
		I_LOG("[hpu::endRequest->{}] handle use {}ms", jobId, consumeTime);
		status.removeConsumeSum.fetch_add(consumeTime);
		status.removeConsumeCount.fetch_add(1);
		rsp.set_content(toJsonString(resp), ContentType::json);
	}

	void HttpProcessUnit::pollRequest(const Request& req, Response& rsp, const std::string& name) {
		if (status.pollCount.load() > 100) {
			I_LOG("[HPU::Request] receive poll request, num:{}, name:{} -> {}", name, simpleReqInfo(req));
			status.pollCount.store(0);
		}

		PollResponse resp;
		bool ret = mcu->getMpuIdList(resp.jobList);
		if (!ret) {
			DefaultResponse errRsp{ 100, "error" };
			rsp.set_content(toJsonString(errRsp), ContentType::json);
			return;
		}
		resp.jobNumber = resp.jobList.size();
		status.pollCount.fetch_add(1);
		status.pollSum.fetch_add(1);
		rsp.set_content(toJsonString(resp), ContentType::json);
	}

	void HttpProcessUnit::queryBaseRequest(const Request& req, Response& rsp, const std::string& name) {
		QueryBaseResponse resp{};
		mcu->getMpuBase(resp.jobNum, resp.chnlNum);
		resp.cpu = cpuQuery.getCurrentCPUUsage();
		resp.mem = seeker::file::getVmRSS();
		resp.startTime = startTimePoint;
		resp.workTime = (seeker::time::currentTime() - serverStartTime) * 0.001;

		rsp.set_content(toJsonString(resp), ContentType::json);
	}

	void HttpProcessUnit::queryListRequest(const Request& req, Response& rsp, const std::string& name) {
		QueryInfoResponse resp{};
		mcu->getMpuInfo(resp.list);
		rsp.set_content(toJsonString(resp), ContentType::json);
	}
}