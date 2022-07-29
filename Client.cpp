// Client.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "sstream"
#include "string"
#include "fstream"
#include <experimental/filesystem>
#define ENDING_DELIMITER '#'

#define BUFF_SIZE 2048
const int PACKET = 1024 * 50; // 50kb

#pragma comment (lib,"ws2_32.lib")

using namespace std;
namespace fs = experimental::filesystem;

//All function is used in program (parameter meaning after main function)
void Option();
void Login();
void Logout();
void Register();
void Upload();
void Download();
void ListFile();
void CreateFolder();
void Delete();
void sendFile(FILE* fp);
int Send(SOCKET s, char *buff, int size, int flags);
int Receive(SOCKET s, char *buff, int size, int flags);
SOCKET client;

int port = 5500;
char* ip = "127.0.0.1";
string root;

int main(int argc, char* argv[])
{
	//Step 1: Inittiate WinSock
	WSADATA wsaData;
	WORD wVersion = MAKEWORD(2, 2);
	if (WSAStartup(wVersion, &wsaData)) {
		printf("Winsock 2.2 is not supported!\n");
		return 0;
	}

	//Step 2: Construct socket

	client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (client == INVALID_SOCKET) {
		printf("ERROR %d: Cannot create client socket. ", WSAGetLastError());
		return 0;
	}
	
	printf("Client started!\n");

	//Step 3: Specify server address
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	inet_pton(AF_INET, ip, &serverAddr.sin_addr);

	//Step 4: Request to connect server
	if (connect(client, (sockaddr *)&serverAddr, sizeof(serverAddr))) {
		printf("Error %d: Cannot connect server.", WSAGetLastError());
		return 0;
	}

	printf("Connected server!\n");

	//Step 5: Communicate with server

	char key;
	while (1) {
		Option();
		cin >> key;
		cin.ignore();
		//Choose option
		if (key == '1')
		{
			Register();
		}
		else if (key == '2') {
			Login();
		}
		else if (key == '3')
		{
			Logout();
		}
		else if (key == '4')
		{
			Upload();
		}
		else if (key == '5') {
			Download();
		}
		else if (key == '6') {
			ListFile();
		}
		else if (key == '7') {
			CreateFolder();
		}
		else if (key == '8') {
			Delete();
		}
		else cout << "You must choose option from 1 to 4!\n";

	}

	//Step 6: Close socket
	closesocket(client);

	//Step 7: Terminate Winsock
	WSACleanup();

	return 0;
}

//Menu of program
void Option()
{
	cout << "Welcome, choose your option!\n";
	cout << "1. Register\n" << "2. Login\n" << "3. Logout\n" << "4. Upload file\n" 
		 << "5. Download file\n" << "6. List File\n" << "7. Create Directory\n"
		 << "8. Delete\n";
	cout << "\n";
	return;
}

//Function handle Login
void Login()
{
	char sbuff[BUFF_SIZE];
	char rbuff[BUFF_SIZE];
	char name[144];
	char pass[144];
	int ret;
	
	cout << "Enter your username(login): ";
	gets_s(name, 144);
	cout << "Enter your password(login): ";
	gets_s(pass, 144);

	strcpy_s(sbuff, "USER ");
	strcat_s(sbuff, name);
	strcat_s(sbuff, " ");
	strcat_s(sbuff, pass);
	//strcat_s(sbuff, "#");

	ret = Send(client, sbuff, strlen(sbuff), 0);
	ret = Receive(client, rbuff, BUFF_SIZE, 0);
	rbuff[ret] = 0;

	if (!strcmp(rbuff, "21")) {
		printf("Login successfully\n");
		root = name;
	}
	else if (!strcmp(rbuff, "22")) printf("Password or Username is wrong!\n");
	else if (!strcmp(rbuff, "23")) cout << "Account is logined\n";
	else if (!strcmp(rbuff, "92")) cout << "Session is active!\n";

}

//Function handle Logout
void Logout()
{
	char sbuff[BUFF_SIZE];
	char rbuff[BUFF_SIZE];
	int ret;
	strcpy_s(sbuff, "BYE ");
	// strcat_s(sbuff, "#");
	ret = Send(client, sbuff, strlen(sbuff), 0);
	ret = Receive(client, rbuff, BUFF_SIZE, 0);
	rbuff[ret] = 0;

	if (!strcmp(rbuff, "31")) {
		printf("Logout successfully\n");
		root = "";
	}
	else if (!strcmp(rbuff, "91")) printf("You must Login first!\n");
}

//Function handle Post
void Register()
{
	char sbuff[BUFF_SIZE];
	char rbuff[BUFF_SIZE];
	char name[144];
	char pass[144];
	char confirmPass[144];
	int ret;

	strcpy_s(sbuff, "CREATE ");
	cout << "Enter your username(register): ";
	gets_s(name, 144);
	cout << "Enter your password(register): ";
	gets_s(pass, 144);
	cout << "Enter your confirm password(register): ";
	gets_s(confirmPass, 10);

	strcat_s(sbuff, name);
	strcat_s(sbuff, " ");
	strcat_s(sbuff, pass);
	strcat_s(sbuff, " ");
	strcat_s(sbuff, confirmPass);
	//strcat_s(sbuff, "#");
	ret = Send(client, sbuff, strlen(sbuff), 0);
	ret = Receive(client, rbuff, BUFF_SIZE, 0);
	rbuff[ret] = 0;

	if (!strcmp(rbuff, "11")) printf("Register successfully!\n");
	else if (!strcmp(rbuff, "12")) printf("Account is exist!\n");
	else if (!strcmp(rbuff, "13")) cout << "You must fill in your username and password\n";
	else if (!strcmp(rbuff, "14")) cout << "Not match confirm password\n";
	else if (!strcmp(rbuff, "93")) cout << "Error Message!\n";
	else if (!strcmp(rbuff, "99")) cout << "Server error!\n";
}

/**
*	@desc:					get file size in bytes
*	@param	src				file resource
*/
int getBytes(const char* src) {
	
	ifstream file(src);

	if (!file.good()) {
		printf("Mo file khong thanh cong khi lay kich thuoc file!\n");
		exit(0);
	}

	file.seekg(0, ios::end);
	int fSize = file.tellg();
	file.close();

	return fSize;
}

/**
*	@desc:					Check if a file exists or not
*	@param	src				file resource
*/
int CheckFile(string src) {
	ifstream file;

	file.open(src);
	if (file.good()) {
		file.close();
		return 0;
	}

	return -1;
}

/**
*	@desc:					Read each piece of data from the file and send it to the client
*	@param	client			Socket associate with client
*	@return 0 or -1			success or fail
*/
int SendFile(string fileName, int client) {
	ifstream file;
	char data[PACKET];

	file.open(fileName, ios_base::binary);

	while (!file.eof()) {
		file.read(data, PACKET);
		int bytesRead = file.gcount();

		if (send(client, data, bytesRead, 0) < 0) {
			perror("Error in sending data");
			return -1;
		}
	}

	printf("Transmit file successfully!\n");
	return 0;
}

// Upload file to server
void Upload()
{
	if (root == "") {
		printf("You must Login first!\n");
		return;
	}

	int ret;
	char rbuff[BUFF_SIZE];

	string src, fName;
	string fDes;
	string fSize;

	cout << "Enter filename to upload: ";
	getline(cin, src);

	string path(src);
	path = "Client/" + root + "/" + path;
	cout << path << "\n";

	if (CheckFile(path) == -1) {
		cout << "File doesn't exit\n";
		return;
	}
	else {
		fName = fs::path(path).filename().string();
	}

	cout << "Enter destination to upload file: ";
	getline(cin, fDes);

	fSize = to_string(getBytes(path.c_str()));
	cout << "Size = " << fSize << endl;
	string msg = "PUT " + fName + " " + fDes + " " + fSize;

	ret = send(client, msg.c_str(), msg.length(), 0);
	ret = recv(client, rbuff, BUFF_SIZE, 0);
	rbuff[ret] = 0;
	
	if (!strcmp(rbuff, "42")) {
		printf("Deny file submission!\n");
		return;
	}
	if (!strcmp(rbuff, "91")) {
		printf("You must Login first!\n");
		return;
	}

	if (!strcmp(rbuff, "41")) {
		printf("Accept uploading file.\n");

		SendFile(path, client);

		ret = recv(client, rbuff, BUFF_SIZE, 0);
		rbuff[ret] = 0;
		if (!strcmp(rbuff, "43"))
			printf("Send file successfully!\n");
		else if (!strcmp(rbuff, "44")) 
			printf("Send file failed!");
	}
}

// Download file from server 
void Download() {
	if (root == "") {
		printf("You must Login first!\n");
		return;
	}

	// Nhap file can tai ve
	// Eg: workplace /test.txt => /Debug/Server/user/test.txt
	string src;
	printf("Enter the file to download: ");
	getline(cin, src);

	string msg = "GET " + src;
	
	if (send(client, msg.c_str(), msg.length(), 0) < 0) {
		printf("Server error!\n");
		exit(0);
	}

	char rBuff[BUFF_SIZE];
	int ret = recv(client, rBuff, BUFF_SIZE, 0);
	if (ret < 0) {
		printf("Server error!\n");
		exit(0);
	}

	rBuff[ret] = 0;
	
	char code[3];
	code[0] = rBuff[0];
	code[1] = rBuff[1];
	code[2] = 0;
	if (!strcmp(code, "52")) {// resource does not exist
		printf("The file to download does not exist\n");
		return;
	}

	int sizeInBytes;
	if (!strcmp(code, "51")) {// Accept to download
		string s(rBuff + strlen("51 "));
		sizeInBytes = stoi(s);
		printf("Accept to download %d bytes\n", sizeInBytes);
	}
	
	char data[PACKET];
	string path = "./Client/" + root + src;
	fstream file(path , ios_base::binary | ios_base::out);
	cout << path << endl;

	if (!file.good()) {
		printf("open file for writing failed!\n");
		exit(0);
	}

	int progress = 0;
	int output = -1;
	while (1) {
		int bytesRecv = recv(client, data, PACKET, 0);
		file.write(data, bytesRecv);

		int totalBytesRevc = file.tellg();
		if (totalBytesRevc == sizeInBytes) { // Hoan tat
			cout << "100%" << endl;
			printf("file received successfully!\n");
			break;
		}

		int progress = 1.0 * totalBytesRevc / sizeInBytes * 100;
		if (progress % 10 == 0 && progress != output) {
			output = progress;
			cout << output << "%\t";
		}
	}

	
	file.close();

	ret = recv(client, rBuff, BUFF_SIZE, 0);
	rBuff[ret] = 0;

	if (!strcmp(rBuff, "53")) {
		printf("Download successfully!\n\n");
		return;
	}
}

// List file in a directory
void ListFile() {
	if (root == "") {
		printf("You must Login first!\n");
		return;
	}

	string src;
	cout << "Enter src: ";
	getline(cin, src);
	string msg = "LS " + src;
	
	int ret;
	char rBuff[BUFF_SIZE];
	send(client, msg.c_str(), msg.length(), 0);
	
	ret = recv(client, rBuff, BUFF_SIZE, 0);
	if (ret <= 0) {
		printf("Server error\n");
		exit(0);
	}

	rBuff[ret] = 0;
	cout << " LIST FILE " << src << ":\n";
	cout << rBuff;

	ret = recv(client, rBuff, BUFF_SIZE, 0);
	if (ret <= 0) {
		printf("Server error\n");
		exit(0);
	}

	rBuff[ret] = 0;

	if (!strcmp(rBuff, "71")) {
		printf("\n");
	}
	else if (!strcmp(rBuff, "72")) {
		printf("Directory does not exist!\n\n");
	}
	else if (!strcmp(rBuff, "91")) {
		printf("You must Login first!\n\n");
	}

	Sleep(2000);
}

// Create sub-folder
void CreateFolder() {
	if (root == "") {
		printf("You must Login first!\n");
		return;
	}

	// Request
	string src;
	cout << "Enter path: ";
	getline(cin, src);
	string msg = "MKDIR " + src;
	send(client, msg.c_str(), msg.length(), 0);


	// Response
	int ret;
	char rBuff[BUFF_SIZE];
	ret = recv(client, rBuff, BUFF_SIZE, 0);
	if (ret <= 0) {
		printf("Server error\n");
		exit(0);
	}

	rBuff[ret] = 0;
	
	if (!strcmp(rBuff, "61")) {
		printf("Directory '%s' was successfully created\n", src.c_str());
	}
	else if (!strcmp(rBuff, "62")) {
		printf("Path is invalid!\n\n");
	}
	else if (!strcmp(rBuff, "63")) {
		printf("Fails, directory exist!\n\n");
	}
	else if (!strcmp(rBuff, "91")) {
		printf("You must Login first!\n\n");
	}

	Sleep(2000);
}

// Delete file or folder
void Delete() {
	if (root == "") {
		printf("You must Login first!\n");
		return;
	}

	// request
	string src;
	cout << "Enter path: ";
	getline(cin, src);

	bool delAll;
	if (fs::path(src).has_extension()) { // is a file

	}
	else {
		cout << "Are you sure to delete all files in the folder (y/n)?: ";
		char c;
		do {
			cin >> c;
			if (c == 'y') delAll = true;
			if (c == 'n') delAll = false;
		} while (c != 'y' && c != 'n');

		if (delAll == false) {
			return;
		}
	}
	
	string msg = "DELETE " + src;
	send(client, msg.c_str(), msg.length(), 0);
	
	// reponse
	int ret;
	char rBuff[BUFF_SIZE];
	ret = recv(client, rBuff, BUFF_SIZE, 0);
	if (ret <= 0) {
		printf("Server error\n");
		exit(0);
	}

	rBuff[ret] = 0;

	if (!strcmp(rBuff, "81")) {
		printf("Delete successfully!\n\n");
	}
	else if (!strcmp(rBuff, "82")) {
		printf("Path is invalid!\n\n");
	}
	else if (!strcmp(rBuff, "91")) {
		printf("You must Login first!\n\n");
	}
}

/* The recv() wrapper function */
int Receive(SOCKET s, char *buff, int size, int flags) {
	int n;

	n = recv(s, buff, size, flags);
	if (n == SOCKET_ERROR)
		printf("Error: %d", WSAGetLastError());

	return n;
}

/* The send() wrapper function*/
int Send(SOCKET s, char *buff, int size, int flags) {
	int n;

	n = send(s, buff, size, flags);
	if (n == SOCKET_ERROR)
		printf("Error: %d", WSAGetLastError());

	return n;
}

void sendFile(FILE* fp) {
	char data[BUFF_SIZE] = { 0 };
	int ret;
	while (fgets(data, BUFF_SIZE, fp) != NULL)
	{
		ret = Send(client, data, sizeof(data), 0);
	}
	memset(data, 0, sizeof(data));
}