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

	bool ret = instances.getInstances().size() == 0;
	if (ret)
	{
		string allInstances = "{ \"notifications\": [" + instances.getJSONInstances()  + "] }";

		ret = instances.getJSONInstances().compare("") == 0;
		if (!ret)
		{
			cerr << "Notification instances JSON array is not empty" << endl;
		}
	}
	else
	{
		cerr << "Notification instances count is not 0" << endl;
	}
	delete managerClient;

	exit(!(ret == true)); }, ::testing::ExitedWithCode(0), "");
}
