#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"

using namespace std;

TEST(NotificationService, Instances)
{
EXPECT_EXIT({
	string myName = "myName";

	ManagementClient* managerClient = new ManagementClient("0.0.0.0", 0);
	NotificationManager instances(myName, managerClient, NULL);

	ASSERT_EQ(0, instances.getInstances().size());
	string allInstances = "{ \"notifications\": [" + instances.getJSONInstances()  + "] }";

	ASSERT_EQ(0, instances.getJSONInstances().compare(""));

	delete managerClient;

	exit(0); }, ::testing::ExitedWithCode(0), "");
}
