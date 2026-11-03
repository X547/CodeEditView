#include <Application.h>

#include "DemoWindow.h"


static constexpr const char *kAppSignature = "application/x-vnd.CodeEditDemo";


class DemoApp: public BApplication {
public:
	DemoApp():
		BApplication(kAppSignature)
	{
	}

	void ReadyToRun() final
	{
		(new DemoWindow())->Show();
	}
};


int main()
{
	DemoApp app;
	app.Run();
	return 0;
}
