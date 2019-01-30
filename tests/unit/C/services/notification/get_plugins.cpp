#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"

using namespace std;

TEST(NotificationService, GetPlugins)
{
	string myName = "myName";

	ManagementClient* managerClient = new ManagementClient("0.0.0.0", 0);
	NotificationManager instances(myName, managerClient, NULL);

	// Check for embedded OverMaxRule rule plugin
	ASSERT_TRUE(instances.getJSONRules().find("OverMaxRule") != string::npos);

	// If FOGLAMP_ROOT is set we should have some delivery plugins
	if (!getenv("FOGLAMP_ROOT"))
	{
		ASSERT_EQ(0, instances.getJSONDelivery().compare("{}"));
	}
	else
	{
		// NOTE: Test fails if no delivery plugin have been installed
		ASSERT_TRUE(instances.getJSONDelivery().compare("{}") != 0);
	}

	delete managerClient;
}
