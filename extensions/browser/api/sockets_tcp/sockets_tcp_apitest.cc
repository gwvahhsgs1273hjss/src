// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/command_line.h"
#include "base/memory/ref_counted.h"
#include "base/strings/stringprintf.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/service_manager_connection.h"
#include "content/public/common/service_names.mojom.h"
#include "content/public/test/network_service_test_helper.h"
#include "extensions/browser/api/dns/host_resolver_wrapper.h"
#include "extensions/browser/api/dns/mock_host_resolver_creator.h"
#include "extensions/browser/api/sockets_tcp/sockets_tcp_api.h"
#include "extensions/browser/api_test_utils.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/shell/test/shell_apitest.h"
#include "extensions/shell/test/shell_test.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "mojo/public/cpp/bindings/sync_call_restrictions.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/spawned_test_server/spawned_test_server.h"
#include "services/network/public/cpp/features.h"
#include "services/service_manager/public/cpp/connector.h"

namespace extensions {

const std::string kHostname = "127.0.0.1";

class SocketsTcpApiTest : public ShellApiTest {
 public:
  SocketsTcpApiTest() : resolver_creator_(new MockHostResolverCreator()) {
    if (base::FeatureList::IsEnabled(network::features::kNetworkService)) {
      base::CommandLine::ForCurrentProcess()->AppendSwitch(
          switches::kUseMockCertVerifierForTesting);
    }
  }

  void SetUpOnMainThread() override {
    ShellApiTest::SetUpOnMainThread();

    // TODO(xunjieli): Figure out what to do with this in-process host resolver
    // which doesn't work with the out-of-process network service.
    HostResolverWrapper::GetInstance()->SetHostResolverForTesting(
        resolver_creator_->CreateMockHostResolver());
  }

  void TearDownOnMainThread() override {
    HostResolverWrapper::GetInstance()->SetHostResolverForTesting(NULL);
    resolver_creator_->DeleteMockHostResolver();

    ShellApiTest::TearDownOnMainThread();
  }

 private:
  // The MockHostResolver asserts that it's used on the same thread on which
  // it's created, which is actually a stronger rule than its real counterpart.
  // But that's fine; it's good practice.
  scoped_refptr<MockHostResolverCreator> resolver_creator_;
};

IN_PROC_BROWSER_TEST_F(SocketsTcpApiTest, SocketsTcpCreateGood) {
  scoped_refptr<api::SocketsTcpCreateFunction> socket_create_function(
      new api::SocketsTcpCreateFunction());
  scoped_refptr<Extension> empty_extension = ExtensionBuilder("Test").Build();

  socket_create_function->set_extension(empty_extension.get());
  socket_create_function->set_has_callback(true);

  std::unique_ptr<base::Value> result(
      api_test_utils::RunFunctionAndReturnSingleResult(
          socket_create_function.get(), "[]", browser_context()));

  ASSERT_EQ(base::Value::Type::DICTIONARY, result->type());
  std::unique_ptr<base::DictionaryValue> value =
      base::DictionaryValue::From(std::move(result));
  int socketId = -1;
  EXPECT_TRUE(value->GetInteger("socketId", &socketId));
  ASSERT_TRUE(socketId > 0);
}

IN_PROC_BROWSER_TEST_F(SocketsTcpApiTest, SocketTcpExtension) {
  std::unique_ptr<net::SpawnedTestServer> test_server(
      new net::SpawnedTestServer(
          net::SpawnedTestServer::TYPE_TCP_ECHO,
          base::FilePath(FILE_PATH_LITERAL("net/data"))));
  EXPECT_TRUE(test_server->Start());

  net::HostPortPair host_port_pair = test_server->host_port_pair();
  int port = host_port_pair.port();
  ASSERT_TRUE(port > 0);

  // Test that connect() is properly resolving hostnames.
  host_port_pair.set_host("lOcAlHoSt");

  ResultCatcher catcher;
  catcher.RestrictToBrowserContext(browser_context());

  ExtensionTestMessageListener listener("info_please", true);

  ASSERT_TRUE(LoadApp("sockets_tcp/api"));
  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(
      base::StringPrintf("tcp:%s:%d", host_port_pair.host().c_str(), port));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(SocketsTcpApiTest, SocketTcpExtensionTLS) {
  network::mojom::NetworkServiceTestPtr network_service_test;
  // If network service is running in a utility process, the cert of the
  // SpawnedTestServer won't be recognized, so inject mock cert verifier through
  // the test helper interface.
  if (base::FeatureList::IsEnabled(network::features::kNetworkService)) {
    content::ServiceManagerConnection::GetForProcess()
        ->GetConnector()
        ->BindInterface(content::mojom::kNetworkServiceName,
                        &network_service_test);
    mojo::ScopedAllowSyncCallForTesting allow_sync_call;
    network_service_test->MockCertVerifierSetDefaultResult(net::OK);
  }

  std::unique_ptr<net::SpawnedTestServer> test_https_server(
      new net::SpawnedTestServer(
          net::SpawnedTestServer::TYPE_HTTPS, net::BaseTestServer::SSLOptions(),
          base::FilePath(FILE_PATH_LITERAL("net/data"))));
  EXPECT_TRUE(test_https_server->Start());

  net::HostPortPair https_host_port_pair = test_https_server->host_port_pair();
  int https_port = https_host_port_pair.port();
  ASSERT_GT(https_port, 0);

  ResultCatcher catcher;
  catcher.RestrictToBrowserContext(browser_context());

  ExtensionTestMessageListener listener("info_please", true);

  ASSERT_TRUE(LoadApp("sockets_tcp/api"));
  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(base::StringPrintf(
      "https:%s:%d", https_host_port_pair.host().c_str(), https_port));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

}  // namespace extensions
