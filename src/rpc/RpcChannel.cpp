/**
  ******************************************************************************
  * @file           : RpcChannel.cpp
  * @author         : xy
  * @brief          : None
  * @attention      : None
  * @date           : 2025/3/21
  ******************************************************************************
  */

#include <hv/TcpClient.h>
#include "RpcChannel.h"
#include "utils/Config.h"
#include "proto/rpc_header.pb.h"
#include "utils/HvProtocol.h"
#include "utils/Zookeeper.h"

void RpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
							google::protobuf::RpcController *controller,
							const google::protobuf::Message *request,
							google::protobuf::Message *response,
							google::protobuf::Closure *done) {

	tinyrpc::RpcHeader rpc_header;
	auto service = method->service();

	auto service_name = service->name();
	auto method_name = method->name();
	std::string args_str;
	request->SerializeToString(&args_str);
	uint32_t args_len = args_str.size();

	rpc_header.set_service_name(service_name);
	rpc_header.set_method_name(method_name);
	rpc_header.set_args_len(args_len);

	// rpc_header 序列化
	std::string rpc_header_str;
	auto ret = rpc_header.SerializeToString(&rpc_header_str);
	if (!ret) {
		controller->SetFailed("rpc_header serialize error");
		return;
	}

	auto send_str = HvProtocol::packMessageAsString(rpc_header_str);    // 打包成协议格式 头部 4字节+内容

	auto new_send_str = HvProtocol::packMessageAsString(send_str + args_str);    // 打包成协议格式 头部 4字节+内容


	std::string zoo_str = "/" + service_name + "/" + method_name;

	//ZkClient
	Zookeeper zk = Zookeeper();
	zk.start();
	auto ip_port = zk.getData(zoo_str);
	std::cout << "ip_port = " << ip_port << std::endl;

	if (ip_port.empty()) {
		controller->SetFailed("get data from zk failed");
		return;
	}

	int idx = ip_port.find_first_of(':');
	std::string rpc_ip = ip_port.substr(0, idx);
	uint16_t rpc_port = std::stoi(ip_port.substr(idx + 1, ip_port.size() - 1 - idx));

	std::cout << "rpc_ip = " << rpc_ip << " rpc_port = " << rpc_port << std::endl;

	hv::TcpClient tcp_client;
	auto conn_fd = tcp_client.createsocket(rpc_port, rpc_ip.c_str());
	if (conn_fd < 0) {
		controller->SetFailed("connect error");
		return;
	}

	auto *server_unpack_setting = new unpack_setting_t();
	memset(server_unpack_setting, 0, sizeof(unpack_setting_t));
	server_unpack_setting->mode = UNPACK_BY_LENGTH_FIELD;
	server_unpack_setting->package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
	server_unpack_setting->body_offset = SERVER_HEAD_LENGTH;
	server_unpack_setting->length_field_offset = SERVER_HEAD_LENGTH_FIELD_OFFSET;
	server_unpack_setting->length_field_bytes = SERVER_HEAD_LENGTH_FIELD_BYTES;
	server_unpack_setting->length_field_coding = ENCODE_BY_BIG_ENDIAN;
	tcp_client.setUnpack(server_unpack_setting);

	tcp_client.onConnection = [new_send_str](const auto &channel) {
	  if (channel->isConnected()) {
		  std::string peerAddr = channel->peeraddr();
		  printf("connected to %s! conn_fd=%d\n", peerAddr.c_str(), channel->fd());
		  channel->write(new_send_str);
	  } else {
		  printf("connect failed\n");
	  }
	};

	tcp_client.onMessage = [response](const auto &channel, const auto &buf) {

	  std::cout << "client onMessage" << std::endl;

	  std::string data = std::string((char *)buf->data(), buf->size());

	  std::string actual_data;
	  auto len = HvProtocol::unpackMessage(data, actual_data);

	  std::cout << actual_data << std::endl;
	  response->ParseFromArray(actual_data.c_str(), len);
	};

	tcp_client.start();

	while (getchar() != '\n');
}