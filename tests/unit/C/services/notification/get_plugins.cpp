#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"
#include <dirent.h>

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
		string deliveryPluginDir(getenv("FOGLAMP_ROOT"));
		deliveryPluginDir += "/plugins/notificationDelivery";
		DIR* dir = opendir(deliveryPluginDir.c_str());
		bool pluginsFound = false;
		if (dir)
		{
			struct dirent *entry;
			while ((entry = readdir(dir)))
			{
				if (strcmp (entry->d_name, "..") != 0 &&
				    strcmp (entry->d_name, ".") != 0)
				{
					pluginsFound = true;
				}
			}
			closedir(dir);
		}

		if (pluginsFound)
		{
			// Check array is NOT empty
			ASSERT_TRUE(instances.getJSONDelivery().compare("[]") != 0);
		}
		else
		{
			// Check array must be empty
			ASSERT_TRUE(instances.getJSONDelivery().compare("[]") == 0);
		}
	}

	delete managerClient;
}
