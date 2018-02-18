//-----------------------------------------------------------------------------
// Class:	ParaEngineService
// Authors:	LiXizhi
// Emails:	LiXizhi@yeah.net
// Company: ParaEngine Corporation
// Date:	2009.7.26
// Desc: Cross platformed. 
//-----------------------------------------------------------------------------
#include "ParaEngine.h"
#ifndef PARAENGINE_MOBILE
#include "util/ParaTime.h"
#include <boost/bind.hpp>
#include "util/keyboard.h"
#include "NPLRuntime.h"
#include "INPLAcitvationFile.h"
#include "IParaEngineApp.h"
#include "ParaEngineService.h"

using namespace ParaEngine;

/** 300 milliseconds per activation, since we use std::time, the smallest value is 1 second.  */
#define MAIN_TIMER_DURATION		100

/** this is only used for termination signal processing. */
ParaEngine::CParaEngineService * g_current_service_ptr = NULL;

CParaEngineService::CParaEngineService()
: m_main_timer(m_main_io_service), m_bQuit(false), m_pParaEngineApp(NULL), m_bAcceptKeyStroke(true)
{
	g_current_service_ptr = this;
}

CParaEngineService::~CParaEngineService()
{

}

void CParaEngineService::StopService()
{
	APP_LOG("service is stopped");
	m_work_lifetime.reset();
	m_bQuit = true;
}

void CParaEngineService::AcceptKeyStroke(bool bAccept)
{
	m_bAcceptKeyStroke = bAccept;
}

/** main loop handler define a INPLActivationFile handler in C++. 
* a main loop file to make sure the FrameMove is called in the NPL thread instead of main thread
*/
class CNPLFile_ServerMainLoop : public NPL::INPLActivationFile
{
public:

	CNPLFile_ServerMainLoop(IParaEngineApp* pParaEngineApp):m_pParaEngineApp(pParaEngineApp), m_fElapsedTime(0.f){};
	
	virtual NPL::NPLReturnCode OnActivate(NPL::INPLRuntimeState* pState){
		if(m_pParaEngineApp)
		{
			m_fElapsedTime += (float)(MAIN_TIMER_DURATION / 1000.f);
			if(m_pParaEngineApp->GetAppState() != ParaEngine::PEAppState_Exiting)
			{
				m_pParaEngineApp->FrameMove(m_fElapsedTime);
			}
		}
		return NPL::NPL_OK;
	};

protected:

	IParaEngineApp* m_pParaEngineApp;
	/** number of seconds elapsed since the game engine start. */
	float m_fElapsedTime;
};
	
void CParaEngineService::handle_timeout(const boost::system::error_code& err)
{
	if (!err && !m_bQuit && m_pParaEngineApp!=0)
	{
		// APP_LOG("heart beat");

#ifndef PARAENGINE_MOBILE
		if(m_bAcceptKeyStroke)
		{
			if(_kbhit())
			{
				// key handler only under windows. 
				char ans = getchar();
				if(ans == 'p')
				{
					// pause now?
				}
				else if (ans == 10)
				{
					StopService();
					return;
				}
			}
		}
#endif
		if (m_pParaEngineApp->GetAppState() == ParaEngine::PEAppState_Exiting)
		{
			StopService();
			return;
		}

		ParaEngine::CGlobals::GetNPLRuntime()->GetMainState()->activate("ParaEngineService.cpp", "");

		// continue with next activation. 
		m_main_timer.expires_from_now(boost::chrono::milliseconds(MAIN_TIMER_DURATION));
		m_main_timer.async_wait(boost::bind(&CParaEngineService::handle_timeout, this, boost::asio::placeholders::error));
	}
	else
	{
		StopService();
	}
}

/* signal handler function */
void CParaEngineService::Signal_Handler(int sig)
{
#ifndef WIN32
	switch(sig)
	{
	case SIGTERM:
		/* finalize the server */
		if(g_current_service_ptr)
		{
			g_current_service_ptr->StopService();
		}
		// exit(0);
		break;
	}
#endif
}



/*
When a daemon starts up, it has to do some low-level housework to get itself ready for its real job. This involves a few steps:

Fork off the parent process
Change file mode mask (umask)
Open any logs for writing
Create a unique Session ID (SID)
Change the current working directory to a safe place
Close standard file descriptors
Enter actual daemon code
*/
void CParaEngineService::InitDaemon(void)
{
#ifndef WIN32
	int i;
	pid_t pid, sid;

	if( signal( SIGINT, SIG_IGN ) != SIG_IGN )
		signal( SIGINT, SIG_IGN );
	if( signal( SIGKILL, SIG_IGN ) != SIG_IGN )
		signal( SIGKILL, SIG_IGN );

	/* Fork off the parent process */
	if ((pid = fork()) == 0)
	{
		/* Fork a child process, sending the daemon into the background loop.*/

		/* By setting the umask to 0, we will have full access to the files generated by the daemon*/
		umask(0);

		/* Create a new SID for the child process */
		sid = setsid();
		if (sid < 0) 
		{
			/* Log any failure */
			exit(EXIT_FAILURE);
		}

		// whether we use root directory or current directory, we will assume current directory. 
		bool daemon_use_root_dir = false;
		if(daemon_use_root_dir)
		{
			/* Change the current working directory */
			if ((chdir("/")) < 0) {
				/* Log any failure here */
				exit(EXIT_FAILURE);
			}
		}

		/* Close out the standard file descriptors */
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

		/** now entering daemon code, Be as verbose as possible when writing either to the syslog or your own logs. 
		Debugging a daemon can be quite difficult when there isn't enough information available as to the status of the daemon.*/

		signal(SIGTERM, ParaEngine::CParaEngineService::Signal_Handler); /* software termination signal from kill */
	}
	else
	{
		/* Exit parent process */
		if( signal( SIGINT, SIG_DFL ) != SIG_DFL )
			signal( SIGINT, SIG_DFL );
		if( signal( SIGKILL, SIG_DFL ) != SIG_DFL )
			signal( SIGKILL, SIG_DFL );
		exit(0);
	}
#endif
}


int CParaEngineService::Run(const char* pCommandLine, IParaEngineApp* pApp)
{
	if (pApp && !pCommandLine){
		pCommandLine = pApp->GetAppCommandLine();
	}


	assert(pApp);
	m_pParaEngineApp = pApp;
	bool bInteractiveMode = strcmp("true", m_pParaEngineApp->GetAppCommandLineByParam("i", "false")) == 0;
	if (bInteractiveMode)
	{
		// run in interpreter and interactive mode. io.read() can be used to read input from standard io. 
		AcceptKeyStroke(false);
	}
	else
	{
		// printf("            ---ParaEngine Server V%d.%d---         \n", 2, 0);
		printf("            ---ParaEngine Service---               \n");
		printf("---------------------------------------------------\n");
		printf("Service is now running ... do not exit             \n");
		printf("cmd line: %s \n", pCommandLine ? pCommandLine : "");
		printf("---------------------------------------------------\n");
		if (IsAcceptKeyStroke())
		{
			printf("            Press ENTER Key to exit the program    \n");
		}
	}

	m_pParaEngineApp->Init(0);

	// this makes the server more responsive to NPL messages. However, JGSL is still lagged. 
	ParaEngine::CGlobals::GetNPLRuntime()->SetHostMainStatesInFrameMove(false);

	// a main loop file to make sure the FrameMove is called in the NPL thread instead of main thread
	auto main_loop_file = new CNPLFile_ServerMainLoop(m_pParaEngineApp);
	ParaEngine::CGlobals::GetNPLRuntime()->GetMainState()->RegisterFile("ParaEngineService.cpp", main_loop_file);

	m_work_lifetime.reset(new boost::asio::io_service::work(m_main_io_service));
	m_main_timer.expires_from_now(boost::chrono::milliseconds(50));
	m_main_timer.async_wait(boost::bind(&CParaEngineService::handle_timeout, this, boost::asio::placeholders::error));

	// start the service now
	m_main_io_service.run();

	// stop the service now. 
	m_pParaEngineApp->FinalCleanup();

	if (!bInteractiveMode)
		printf("Service is stopped             \n");
	int return_code = m_pParaEngineApp->GetReturnCode();
	if (!pApp){
		SAFE_DELETE(m_pParaEngineApp);
	}
	return return_code;
}
#endif