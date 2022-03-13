/* Laszlo Frazer Data Acquisition Program 2014 
For 64 bit AMD instruction sets
*/

#include <stdio.h>              // for printf()
#include "sicl.h"		// Standard Instrument Control Library routines, Agilent
#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <assert.h>
#include "stdafx.h"
#include <phidget21.h> //motor control library, Phidgets

#define DEVICE_ADDRESS "gpib0,1"   // Modify this line to match your setup

#define READS 2//how much averaging to do
//apparently more than 249 causes stack overflow

//motor configuration
#define OPENPOS 76.00
#define CLOSEDPOS 110.00
//time it takes motor to move
#define DWELLTIME 4000

//responds to attachment of phidget
int CCONV AttachHandler(CPhidgetHandle ADVSERVO, void *userptr)
{
	int serialNo;
	const char *name;

	CPhidget_getDeviceName (ADVSERVO, &name);
	CPhidget_getSerialNumber(ADVSERVO, &serialNo);
	printf("%s %10d attached!\n", name, serialNo);

	return 0;
}

//responds to disconnection of phidget
int CCONV DetachHandler(CPhidgetHandle ADVSERVO, void *userptr)
{
	int serialNo;
	const char *name;

	CPhidget_getDeviceName (ADVSERVO, &name);
	CPhidget_getSerialNumber(ADVSERVO, &serialNo);
	printf("%s %10d detached!\a\n", name, serialNo);
	getchar();

	return 0;
}

//responds to error
int CCONV ErrorHandler(CPhidgetHandle ADVSERVO, void *userptr, int ErrorCode, const char *Description)
{
	printf("Error handled. %d - %s\n", ErrorCode, Description);
	getchar();
	return 0;
}

//start of program
int main(void)
{
	INST id;                 	// device session id
	char buf[256] = { 0 };   	// read buffer for idn string
	char buf2[256] = { 0 };
	char buf3[256] = { 0 };
	char buf4[256] = { 0 };


	char curvebuf[10000] = { 0 }; //big buffer for data from oscilloscope
	char busybuf[256] = { 0 };
	char one[1] = {0};
	char voltage[1024] = {0}; //voltage scale
	char time[1024] = {0}; //time scale
	int i=0;
	int j=0;
	int k=0;
	long int number=0; //for strtol
	char * charptr; //for strtol
	long int array[READS][1000];//signal data
	long int barray[READS][1000];//background data
	double average[1000] = {0};//signal average
	double baverage[1000] = {0};//background average
	errno_t error;

	//command line access variables
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	//file management
	FILE * data;
	FILE * gnuplot;
#if defined(__BORLANDC__) && !defined(__WIN32__)
	_InitEasyWin();		// required for Borland EasyWin programs.
#endif

	//phidget declarations
	int result;
	double curr_pos;
	const char *err;
	double maxVel, maxAccel;
	int stopped;

	//Declare an advanced servo handle
	CPhidgetAdvancedServoHandle servo = 0;

	//create the advanced servo object
	CPhidgetAdvancedServo_create(&servo);

	//Set the handlers to be run when the device is plugged in or opened from software, unplugged or closed from software, or generates an error.
	CPhidget_set_OnAttach_Handler((CPhidgetHandle)servo, AttachHandler, NULL);
	CPhidget_set_OnDetach_Handler((CPhidgetHandle)servo, DetachHandler, NULL);
	CPhidget_set_OnError_Handler((CPhidgetHandle)servo, ErrorHandler, NULL);

	//Registers a callback that will run when the motor position is changed.
	//Requires the handle for the Phidget, the function that will be called, and an arbitrary pointer that will be supplied to the callback function (may be NULL).
	//CPhidgetAdvancedServo_set_OnPositionChange_Handler(servo, PositionChangeHandler, NULL);

	//open the device for connections
	CPhidget_open((CPhidgetHandle)servo, -1);

	//get the program to wait for an advanced servo device to be attached
	printf("Waiting for Phidget Advanced Servo to be attached....\n");
	if((result = CPhidget_waitForAttachment((CPhidgetHandle)servo, 10000)))
	{
		CPhidget_getErrorDescription(result, &err);
		printf("Problem waiting for attachment: %s\n", err);
		getchar();
		return 0;
	}

	//Display the properties of the attached device
	//display_properties(servo);

	//read event data
	printf("Reading.....\n");

	//This example assumes servo motor is attached to index 0

	//Set up some initial acceleration and velocity values
	CPhidgetAdvancedServo_getAccelerationMax(servo, 0, &maxAccel);
	CPhidgetAdvancedServo_setAcceleration(servo, 0, maxAccel/200);
	CPhidgetAdvancedServo_getVelocityMax(servo, 0, &maxVel);
	CPhidgetAdvancedServo_setVelocityLimit(servo, 0, maxVel/100);


	//power up motor
	CPhidgetAdvancedServo_setEngaged(servo, 0, 1);



	// Install a default SICL error handler that logs an error message and
	// exits.  On Windows 95 view messages with the SICL Message Viewer,
	// and on Windows NT use the Windows NT Event Viewer.
	ionerror(I_ERROR_EXIT);	

	// Open a device session using the DEVICE_ADDRESS

	id = iopen(DEVICE_ADDRESS);

	// Set the I/O timeout value for this session to 1 second
	itimeout(id, 1000);

	// Write the *RST string (and send an EOI indicator) to put the instrument
	// in a known state.

	//iprintf(id, "*RST\n");

	// Write the *IDN? string and send an EOI indicator, then read
	// the response into buf.  
	// For WIN16 programs, this will only work with the Large memory model 
	// since ipromptf expects to receive far pointers to the format strings.

	ipromptf(id, "*IDN?\n", "%t", buf);

	printf("%s\n", buf);

	ipromptf(id,"ACQUIRE:NUMAVG 256",buf2);//set number of traces to average
	ipromptf(id,"ACQUIRE:MODE AVERAGE",buf2);//set averaging mode 
	ipromptf(id,"DATA:WIDTH 2",buf2);//read 2 bytes of data, only works when averaging
	ipromptf(id,"ACQUIRE:STOPAFTER SEQUENCE",buf3);
	//oscilloscope stops after data is collected

	for(k=0;k<READS;k++){
		//move servo
		CPhidgetAdvancedServo_setPosition(servo, 0, OPENPOS);

		Sleep(DWELLTIME);

		printf("Collecting signal.\a");
		ipromptf(id,"ACQUIRE:STATE ON",buf4);//start acquisition

		Sleep(10);
		ipromptf(id,"BUSY?","%t",busybuf);
		one[0]=busybuf[0];//assumes I'm busy, measures the busy message

		//while oscilloscope is busy, sleep
		i=0;
		while(one[0]==busybuf[0]){
			i++;
			if(1==i%10){
				printf("\nBusy for %i seconds with message ",i);
				printf("%s",busybuf);
			} else {
				printf(".");
			}
			Sleep(1000);
			ipromptf(id,"BUSY?","%t",busybuf);

		}
		printf("\nDone after %i seconds with message ",i);
		printf("%s",busybuf);

		Sleep(10);

		ipromptf(id, "CURV?\n", "%t", curvebuf);//read curve from oscilloscope
		//printf(curvebuf);

		j=0;
		for(i=0;(i<sizeof(curvebuf));i++){//gnuplot formatting

			//handle first number
			if((i==0)){
				array[k][j]=strtol(&curvebuf[i],&charptr,0);

				j++;
			}

			//handle remaining numbers
			if(curvebuf[i]==*","){
				assert(i!=0);
				curvebuf[i]='\n';	
				array[k][j]=strtol(&curvebuf[i],&charptr,0);
				j++;
			}
			//else we're done
		}

		//move servo
		CPhidgetAdvancedServo_setPosition (servo, 0, CLOSEDPOS);
		
		Sleep(DWELLTIME);

		printf("Collecting background.\a");
		ipromptf(id,"ACQUIRE:STATE ON",buf4);//start acquisition

		Sleep(10);
		ipromptf(id,"BUSY?","%t",busybuf);
		one[0]=busybuf[0];//assumes I'm busy, measures the busy message

		i=0;
		while(one[0]==busybuf[0]){
			i++;
			if(1==i%10){
				printf("\nBusy for %i seconds with message ",i);
				printf("%s",busybuf);
			} else {
				printf(".");
			}
			Sleep(1000);
			ipromptf(id,"BUSY?","%t",busybuf);

		}
		printf("\nDone after %i seconds with message ",i);
		printf("%s",busybuf);
		
		Sleep(10);

		ipromptf(id, "CURV?\n", "%t", curvebuf);//read curve from oscilloscope
		//printf(curvebuf);

		j=0;
		for(i=0;(i<sizeof(curvebuf));i++){//gnuplot formatting
			//handle first number
			if((i==0)){
				barray[k][j]=strtol(&curvebuf[i],&charptr,0);

				j++;
			}

			//handle remaining numbers
			if(curvebuf[i]==*","){
				assert(i!=0);
				curvebuf[i]='\n';	
				barray[k][j]=strtol(&curvebuf[i],&charptr,0);
				j++;
			}
		}

		printf("%i reads remaining.\n",READS-k-1);

	}

	//sum up measurements
	for(k=0;k<READS;k++){
		for(i=0;i<1000;i++){

			average[i]+=(double)array[k][i];
			baverage[i]+=(double)barray[k][i];
		}
	}

	//divide to get average
	for(i=0;i<1000;i++){
		//printf("%f\n",average[i]);
		average[i]=average[i]/((double)READS);
		baverage[i]=baverage[i]/((double)READS);
		//printf("%f\n",average[i]);
	}


	error=fopen_s(&data,"signal.csv","w");
	if(error!=0){
		printf("Data file not opened.\n");
	}

	error=fopen_s(&gnuplot,"plot.gp","w");
	if(error!=0){
		printf("Plot file not opened.\n");
	}
	ipromptf(id, "CH1:SCA?\n", "%t", voltage);//measure voltage per division
	printf("%s", voltage);
	ipromptf(id, "hor:mai?\n", "%t", time);//measure time per division
	printf("%s", time);


	//make gnuplot file
	fprintf(gnuplot,"set terminal windows position 1,1\n");

	//only valid for 2 byte mode
	fprintf(gnuplot,"plot \"subtract.csv\" using ($0*%s/50):($1*%s/25/256) with lines\n", strtok(time,"\n"),strtok(voltage,"\n"));

	//display graph briefly
	fputs("pause 3",gnuplot);

	//make data file
	for(i=0;i<1000;i++){
		fprintf(data,"%f\n",average[i]);
	}

	//close files
	if(fclose(data)!=0){
		printf("File not closed.");
	}
	if(fclose(gnuplot)!=0){
		printf("File not closed.");
	}

	//make background file
	error=fopen_s(&data,"back.csv","w");
	if(error!=0){
		printf("Data file not opened.\n");
	}
	for(i=0;i<1000;i++){
		fprintf(data,"%f\n",baverage[i]);
	}
	if(fclose(data)!=0){
		printf("File not closed.");
	}

	//make subtracted file
	error=fopen_s(&data,"subtract.csv","w");
	if(error!=0){
		printf("Data file not opened.\n");
	}
	for(i=0;i<1000;i++){
		fprintf(data,"%f\n",average[i]-baverage[i]);
	}
	if(fclose(data)!=0){
		printf("File not closed.");
	}


	//reset si
	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );

	//draw graph
	if( !CreateProcess(NULL,    // No module name (use command line)
		"C:\\Program Files (x86)\\gnuplot\\bin\\gnuplot.exe -e \"load \\\"plot.gp\\\"\"",//argv[1],        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&(si),            // Pointer to STARTUPINFO structure
		&(pi) )           // Pointer to PROCESS_INFORMATION structure
		) 
	{
		printf( "CreateProcess failed (%d).\n", GetLastError() );
	}

	//turn off motor
	CPhidgetAdvancedServo_setEngaged(servo, 0, 0);

	//disconnect phidget control board
	CPhidget_close((CPhidgetHandle)servo);
	CPhidget_delete((CPhidgetHandle)servo);

	//close session with GPIB device
	iclose(id);

	//let user check output
	printf("\nDone.\a\a\a\a\n");
	getchar();


	// For WIN16 programs, call _siclcleanup before exiting to release 
	// resources allocated by SICL for this application.  This call is a
	// no-op for WIN32 programs.
	_siclcleanup();
	return(0);
}
