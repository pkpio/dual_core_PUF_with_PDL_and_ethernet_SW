// Copyright: Microsoft 2009
//
// Original Author: Ken Eguro and Sandro Forin
// Includes code written by 2009 MSR intern Rene Mueller.
//
// Modified by : Praveen Kumar Pendyala
// Created  : 05/27/13 
// Modified : 06/03/13
//
// Description:
// Send 256 bits of data.
//	0-127 correspond to challenge A
//	128-255 correspnd to challenge B
//
//	Bugs
//	Due to a bug in the hardware code responses actually start from memory address 01 and till memory addresss 32. So we read back 1 bit more to adjust for the offset.

//Enter operands here
//32-bit operands integer
//Hex value is AAAA AAAA
//Binary equivalent will be 101010101010.....101010 32-bits
#define A 2863311530
#define B 2863311530
#define WRITE_SIZE 1024	// 16(# config bytes per bit) * 64 (total # in both cores combined)
#define READ_SIZE 4		// 32-bit results is read back as 4 8-bit values

#include <windows.h>
#include <WinIoctl.h>
#include <setupapi.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <iomanip>
#include <assert.h>
#include <list>
#include <vector>
#include <time.h>
#include <direct.h>

#include "sirc_internal.h"
#include "display.h"

using namespace std;
using namespace System;
using namespace System::IO;
using namespace System::Collections;

void error(string inErr){
	cerr << "Error:" << endl;
	cerr << "\t" << inErr << endl;

    PrintZeLog();
	exit(-1);
}

int main(int argc, char* argv[]){
	int i=0;

	//Operands
	int OpA = A;
	int OpB = B;

	//Configuration bits for each bit in each core
	int core0[32][16];
	int core1[32][16];

	//Assign configuration bits here. This is temporary. Will be read from file later.
	//core0[0][]
	
	int challengeA[16] = {17,1,2,3,4,5,6,7,8,9,10,11,12,13,1,15};
	int challengeB[16] = {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,19};	

	//Test harness variables
	ETH_SIRC *SIRC_P;
	uint8_t FPGA_ID[6];
	bool FPGA_ID_DEF = false;

	uint32_t numOps = 32;
	uint32_t numOpsReturned;
	uint32_t tempInt;
    uint32_t driverVersion = 0;

	char *token = NULL;
	char *next_token = NULL;
	uint32_t val;
	int waitTimeOut = 0;

	//Input buffer
	uint8_t *inputValues;
	//Output buffer
	uint8_t *outputValues;

	//Speed testing variables
	DWORD start, end;

	//Dummy variable for user action
	char pressAnyKey;

	std::ostringstream tempStream;

#ifndef BUGHUNT
    StartLog();
#endif

	//**** Process command line input.
	//	We may ignore this. This is just to facilitate passing few params like FPGA id, numOps, waitTime and bandwidth params through terminal
	if (argc > 1) {
		cout<<"Passed arguments while executing.." << endl;
		for(int i = 1; i < argc; i++){
			if(strcmp(argv[i], "-mac") == 0){ 
				if(argc <= i + 1){
					tempStream << "-mac option requires argument";
					error(tempStream.str());					
				}
				
				//Map the target MAC address to an FPGA id.
				val = hexToFpgaId(argv[i + 1],FPGA_ID, sizeof(FPGA_ID));
				//Check to see if there were exactly 6 bytes
				if (val!=6) {
					tempStream << "Invalid MAC address. macToFpgaId returned " << i;
					error(tempStream.str());
				}

				cout << "destination MAC: "
						<< hex << setw(2) << setfill('0') 
						<< setw(2) << (int)FPGA_ID[0] << ":" 
						<< setw(2) << (int)FPGA_ID[1] << ":" 
						<< setw(2) << (int)FPGA_ID[2] << ":" 
						<< setw(2) << (int)FPGA_ID[3] << ":" 
						<< setw(2) << (int)FPGA_ID[4] << ":" 
						<< setw(2) << (int)FPGA_ID[5] << dec << endl;
				FPGA_ID_DEF = true;
				i++;
			}
			else if (strcmp(argv[i], "-waitTimeOut") == 0){
				if(argc <= i + 1){
					tempStream << "-waitTime option requires argument";
					error(tempStream.str());					
				}
				waitTimeOut = (uint32_t) atoi(argv[i + 1]);
				if ((waitTimeOut < 1)) {
					tempStream << "Invalid waitTime: " << waitTimeOut << ".  Must be >= 1";
					error(tempStream.str());
				}
				i++;
			}
			else{
				tempStream << "Unknown option: " << argv[i] << endl;
				tempStream << "Usage: " << argv[0] << " {-mac X:X:X:X:X:X} {-waitTimeOut X}" << endl;
				error(tempStream.str());
			}
		}
	} 
	if(!FPGA_ID_DEF){
		cout << "****USING DEFAULT MAC ADDRESS - AA:AA:AA:AA:AA:AA" << endl;
		FPGA_ID[0] = 0xAA;
		FPGA_ID[1] = 0xAA;
		FPGA_ID[2] = 0xAA;
		FPGA_ID[3] = 0xAA;
		FPGA_ID[4] = 0xAA;
		FPGA_ID[5] = 0xAA;
	}

	//**** Set up communication with FPGA
	//Create communication object
	SIRC_P = new ETH_SIRC(FPGA_ID, driverVersion, NULL);
	//Make sure that the constructor didn't run into trouble
    if (SIRC_P == NULL){
		tempStream << "Unable to find a suitable SIRC driver or unable to ";
		error(tempStream.str());
    }
	if(SIRC_P->getLastError() != 0){
		tempStream << "Constructor failed with code " << (int) SIRC_P->getLastError();
		error(tempStream.str());
	}

	
	//Reading challenges from challege.txt
	string challengeVector;
	ifstream challengesFile ("challenges.txt");
	if (challengesFile.is_open()){
		cout << endl << "Found challenges file. Attempting to read challenges from file..." << endl;
		while (challengesFile.good() ){
			getline (challengesFile,challengeVector);
			if(i==0) cout << "Line 1 is : " << challengeVector << endl;
			if(i==1) cout << "Line 2 is : " << challengeVector << endl;
			i++;
		}
		challengesFile.close();
	} else{
		cout << endl << "Unable to open file challenges file. Assigning default challenges.." << endl;
		//Default values will be used
	}


    //Get runtime parameters, for what we wont change.
    SIRC::PARAMETERS params;
    if (!SIRC_P->getParameters(&params,sizeof(params))){
		tempStream << "Cannot getParameters from SIRC interface, code " << (int) SIRC_P->getLastError();
		error(tempStream.str());
    }

    //These MUST match the buffer sizes in the hw design.
    params.maxInputDataBytes  = 1<<17; //2**17 128KBytes
    params.maxOutputDataBytes = 1<<13; //2**13 8KBytes

	if(waitTimeOut != 0){
		params.writeTimeout = waitTimeOut;
		params.readTimeout = waitTimeOut;
	}

	if (!SIRC_P->setParameters(&params,sizeof(params))){
		tempStream << "Cannot setParameters on SIRC interface, code " << (int) SIRC_P->getLastError();
		error(tempStream.str());
    }

    //Fill up the input buffer
    if (numOps == 0){
       numOps = min(params.maxInputDataBytes, params.maxOutputDataBytes);
	}
	else if(numOps > params.maxInputDataBytes || numOps > params.maxOutputDataBytes){
		tempStream << "Invalid number of operations defined, must be less than or equal to " 
			<< (int)  min(params.maxInputDataBytes, params.maxOutputDataBytes);
		error(tempStream.str());
	}

	cout << endl << endl << "Doing a soft rest before proceeding..." << endl;
    LogIt(LOGIT_TIME_MARKER);
	if(!SIRC_P->sendReset()){
		tempStream << "Reset failed with code " << (int) SIRC_P->getLastError();
		error(tempStream.str());
	}
	cout << "Soft reset passed !" << endl << endl;

	//This example circuit takes a few parameters:
	//	Param Register 0 - number of input bytes
	//	Param Register 1 - 32-bit multipler
	//	Input buffer - N bytes to be multipled
	//	Expected output - N bytes equal to (input values * multipler) % 256
	cout << "Data sending phase started..." << endl;

	inputValues = (uint8_t *) malloc(sizeof(uint8_t) * numOps);
	assert(inputValues);
	outputValues = (uint8_t *) malloc(sizeof(uint8_t) * (numOps+1));
	assert(outputValues);

	//Set input values to challengeA followed by challengeB
	cout<<"Reading in challeneges to inputvalue"<<endl;
	
	cout<<"Challenge A is : ";
	for(int i = 0; i < 16; i++){
		inputValues[i] = challengeA[i];
		cout<<(int)inputValues[i]<<", ";
	}
	
	cout<<endl<<"Challenge B is : ";
	for(int i = 0; i < 16; i++){
		inputValues[i+16] = challengeB[i];
		cout<<(int)inputValues[i+16]<<", ";
	}
	
	cout<<endl<<"End of inputs. Data ready."<<endl<<endl;


    LogIt(LOGIT_TIME_MARKER);
	start = GetTickCount();
	//Set parameter register 0 to the number of operations
	if(!SIRC_P->sendParamRegisterWrite(0, numOps)){
		tempStream << "Parameter register write failed with code " << (int) SIRC_P->getLastError();
		error(tempStream.str());
	}
	//Set parameter register 1 to a multiplier of 3
	if(!SIRC_P->sendParamRegisterWrite(1, 3)){
		tempStream << "Parameter register write failed with code " << (int) SIRC_P->getLastError();
		error(tempStream.str());
	}

	//Next, send the input data
	//Start writing at address 0
	cout<<"Writing inputs to FPGA..."<<endl;
	if(!SIRC_P->sendWrite(0, numOps, inputValues)){
		tempStream << "Write to FPGA failed with code " << (int) SIRC_P->getLastError();
		error(tempStream.str());
	} else{
		cout<<"Write success !"<<endl<<endl;
	}

	//Set the run signal
	cout<<"Issued a run signal"<<endl;
	if(!SIRC_P->sendRun()){
		tempStream << "Run command failed with code " << (int) SIRC_P->getLastError();
		error(tempStream.str());
	} else{
		cout<<"Run command issue success !"<<endl<<endl;
	}

	//Wait up to N seconds for the execution to finish (we can compute ~500M numbers in that time)
	if(waitTimeOut == 0){
		cout<<"Allowed a waitTimeOut of : 10 secs"<<endl;
		if(!SIRC_P->waitDone(10)){
			tempStream << "Wait till done failed with code " << (int) SIRC_P->getLastError();
			error(tempStream.str());
		} else{
			cout<<"User code exectution completed successfully !"<<endl<<endl;
		}
	}
	else{
		cout<<"Allowed a waitTimeOut of : "<< waitTimeOut <<" secs"<<endl<<endl;
		if(!SIRC_P->waitDone(waitTimeOut)){
			tempStream << "Wait till done failed with code " << (int) SIRC_P->getLastError();
			error(tempStream.str());
		} else{
			cout<<"User code exectution completed successfully !"<<endl<<endl;
		}
	}	

	//Read the data back
	cout<<"Atempting to read back responses"<<endl;
	if(!SIRC_P->sendRead(0, (numOps+1), outputValues)){
		tempStream << "Read from FPGA failed with code " << (int) SIRC_P->getLastError();
		error(tempStream.str());
	} else{
		cout<<"Read back from memory success !"<<endl<<endl;
	}
	end = GetTickCount();

	//Verify that the values are correct
	cout<<"The responses are : "<<endl;
	for(int i = 1; i < (numOps+1); i++){
		cout<<(int)outputValues[i]<<", ";
		//if((inputValues[i] * 3) % 256 != outputValues[i]){
		//	tempStream << "Output #" << (int) i << " does not match expected value";
		//	error(tempStream.str());
		//}
	}
	cout<<endl<<"End of Outputs"<<endl<<endl;
	cout << "Operation complete !" << endl;
	cout << "\tExecuted in " << (end - start) << " ms" << endl << endl;


	delete SIRC_P;
	free(inputValues);
	free(outputValues);

#ifdef BUGHUNT
    StartLog(susp);
#endif
    PrintZeLog();
	cout<<endl<<endl<<"Enter any value and press enter to exit";
	cin>>pressAnyKey;
	return 0;
}
