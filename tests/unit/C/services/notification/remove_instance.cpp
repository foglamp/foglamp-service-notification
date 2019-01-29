#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"

using namespace std;

TEST(NotificationService, RemoveInstance)
{
	string myName = "myName";

	ManagementClient* managerClient = new ManagementClient("0.0.0.0", 0);
	NotificationManager instances(myName, managerClient, NULL);

	ASSERT_EQ(0, instances.getInstances().size());
	string allInstances = "{ \"notifications\": [" + instances.getJSONInstances()  + "] }";

	ASSERT_EQ(0, instances.getJSONInstances().compare(""));

	NotificationApi* api = new NotificationApi(0, 1);

	ASSERT_EQ(false, api->removeNotification("NOT_EXISTANT"));

	api->stop();

        delete managerClient;
	delete api;
}
