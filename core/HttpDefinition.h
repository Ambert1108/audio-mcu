// @brief: Http相关参数定义
// @copyright: Copyright seekloud 2024
// @birth: [Ambert@2024.4.29]
// @version: V0.0.1
// @revision: [Ambert@2024.9.9]

#pragma once

namespace aom {
	constexpr auto CREATE_JOB_URL        =   "/audioMcu/createJob";
	constexpr auto ADD_CHANNEL_URL       =   "/audioMcu/addChnl";
	constexpr auto OPEN_MIRCO_URL        =   "/audioMcu/openMic";
	constexpr auto CLOSE_MIRCO_URL       =   "/audioMcu/closeMic";
	constexpr auto REMOVE_CHANNEL_URL    =   "/audioMcu/removeChnl";
	constexpr auto END_JOB_URL           =   "/audioMcu/endJob";
	constexpr auto KEEP_URL							 =	 "/audioMcu/keep";
	constexpr auto QUERY_BASE_URL				 =	 "/audioMcu/queryBase";
	constexpr auto QUERY_LIST_URL				 =	 "/audioMcu/queryList";

	/*
	* 错误码规范：
	*		非程序错误：从-1000开始，例如请求解析错误-1001
	*		程序错误：从1000开始，例如申请UDP端口失败1001
	*/
	constexpr auto PARSER_BODY_ERROR			=			-1001;
	constexpr auto PARSER_FAIL_MSG				=			"parse require body error";
	constexpr auto PARAM_JOBID_EMPTY      =     -1002;
	constexpr auto JOBID_EMPTY_MSG        =     "param jobId is empty";

	constexpr auto APPLY_UDP_PORT_ERROR		=			1001;
	constexpr auto APPLY_PORT_MSG					=			"apply udp port failed";
	constexpr auto JOBID_EXIST_ERROR      =			1002;
	constexpr auto JOBID_EXIST_MSG        =			"jobId is exist";
	constexpr auto JOBID_NOTFOUND_ERROR   =			1003;
	constexpr auto JOBID_NOTFOUND_MSG     =			"jobId is not found";
	constexpr auto JOIN_JOB_ERROR		      =			1004;
	constexpr auto JOIN_JOB_MSG			      =			"join job form failed";
	constexpr auto KEY_PARAM_ERROR        =     1005;
	constexpr auto KEY_PARAM_MSG          =     "key param is wrong";
	constexpr auto CHECK_JOB_ERROR        =     9900;
	constexpr auto NO_JOB_MSG             =     "current no job work";
	constexpr auto UNKNOWN_ERROR					=			9998;
	constexpr auto GET_UNKNOWN_MSG				=			"Server got unknown error";
	constexpr auto GET_EXCEPTION_ERROR		=			9999;
	constexpr auto GET_EXCEPTION_MSG			=			"Server got unknown exception";
}
