#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"

using namespace std;

TEST(NotificationService, CallbackUrl)
{
EXPECT_EXIT({
	string myName = "myName";

	NotificationApi* api = new NotificationApi(0, 1);
	api->setCallBackURL();
	bool ret = api->getCallBackURL().compare("http://127.0.0.1:0/notification/reading/asset/") == 0;

	api->stop();

	delete api;

	exit(!(ret == true)); }, ::testing::ExitedWithCode(0), "");
}
