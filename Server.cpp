#include "stdafx.h"
#include "stdio.h"
#include "stdlib.h"
#include "winsock2.h"
#include "ws2tcpip.h"
#include "process.h"
#include "iostream"
#include "cstdio"
#include "fstream"
#include "map"
#include "direct.h"
#include "string"
#include "sstream"
#include "experimental/filesystem"
using namespace std;
namespace fs = experimental::filesystem;

#pragma comment(lib, "Ws2_32.lib")

const int BUFF_SIZE = 1024 * 50; // 50KB

/* IO OPERATION */
#define RECEIVE 0
#define SEND 1

/* FUNCTION */
#define UPLOAD 2
#define DOWNLOAD 3

/* STRUCT */
typedef struct {
	WSAOVERLAPPED overlapped;
	WSABUF dataBuff;
	char buffer[BUFF_SIZE];
	char archive[BUFF_SIZE];
	int bufLen;
	int operation;
} PER_IO_OPERATION_DATA, *LPPER_IO_OPERATION_DATA;


typedef struct {
	SOCKET	socket;
	string	user;
	fstream file;	// file object
	int		func;	// UPLOAD, DOWNLOAD etc
	int		bytes;	// bytes of file
	int		transferBytes;
} PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

/* Global variable */
int									PORT = 5500;
char*								ip = "127.0.0.1";
int									maxClients = 5000;
// user: {pwd, ?logged}
map<string, pair<string, bool> >	accountData;
CRITICAL_SECTION					criticalSection;

/**
*	@desc:					Get message type from request
*	@param	req				request message
*	@return
*/
string getType(string req) {
	int len = req.length();

	if (len == 0) {
		return "NIL";
	}

	int i;
	for (i = 0; i < len; i++) {
		if (req[i] == ' ') {
			break;
		}
	}

	return req.substr(0, i);
}

/**
*	@desc:		read data about account from database file
*/
void LoadAccount() {
	ifstream file("./Database/account.txt");
	if (!file.good()) {
		return;
	}

	string user, pwd;
	while (file >> user >> pwd) {
		accountData.insert({ user,{ pwd, /*?logged = */ false } });
	}
}

/**
*	@desc:					Read each piece of data from the file and send it to the client
*	@param	client			Socket associate with client
*	@return 0 or -1			success or fail
*/
int SendFile(string fileName, int client) {
	ifstream file;
	char data[BUFF_SIZE];

	file.open(fileName, ios_base::binary);

	while (!file.eof()) {
		file.read(data, BUFF_SIZE);
		int bytesRead = file.gcount();
		cout << "read: " << bytesRead << endl;
		if (send(client, data, bytesRead, 0) < 0) {
			printf("Error in sending data");
			return -1;
		}

	}

	Sleep(5000);
	printf("Transmit file successfully!\n");
	return 0;
}

/**
*	@desc:					handle for file upload message
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
int PutHandler(LPPER_HANDLE_DATA perHandleData, char msg[]) {

	if (perHandleData->user == "") { // not login
		return 91;
	}

	/* get file information from request*/
	stringstream ss(msg);
	string fileName;
	string fileDes;
	int fileSize;
	ss >> fileName >> fileDes >> fileSize;


	string path = "./Server/" + perHandleData->user + fileDes;
	if (fs::is_directory(path) == false) {
		return 42;
	}

	path += "/" + fileName;

	ifstream file(path);
	if (file.good()) { // File exitst
		file.close();
		return 42;
	}

	//	accept file upload from client
	perHandleData->func = UPLOAD;
	perHandleData->bytes = fileSize;
	perHandleData->file.open(path, ios_base::binary | ios_base::out);

	if (!perHandleData->file.good()) {
		printf("File opening failed[PUT HANDLER]\n");
		return -1;
	}

	Sleep(20);
	return 41;
}

/**
*	@desc:					get file size in bytes
*	@param	src				file resource
*/
int getBytes(const char* src) {
	ifstream file(src);

	if (!file.good()) {
		printf("File opening failed when getting file size!\n");
		exit(0);
	}

	file.seekg(0, ios::end);
	int fSize = file.tellg();
	file.close();

	return fSize;
}

/**
*	@desc:					handle for file download message
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
int GetHandler(LPPER_HANDLE_DATA perHandleData, char msg[]) {

	if (perHandleData->user == "") { // not login
		return 91;
	}

	string fileSrc = "./Server/" + perHandleData->user;
	fileSrc.append(msg); // eg. /Server/anhnt

	ifstream file(fileSrc);
	if (!file.good()) {
		printf("File doesn't exit\n");
		return 52; // File doesn't exit
	}

	int sizeInBytes = getBytes(fileSrc.c_str());
	//	Resend download acceptance message with file size
	string str = "51 " + to_string(sizeInBytes);
	send(perHandleData->socket, str.c_str(), str.length(), 0);


	if (SendFile(fileSrc, perHandleData->socket) < 0) {
		// Release socket and session

		EnterCriticalSection(&criticalSection);
		string user = perHandleData->user;
		if (user != "") {
			accountData[user].second = false;
		}
		LeaveCriticalSection(&criticalSection);

		closesocket(perHandleData->socket);
		delete perHandleData;
		perHandleData = NULL;
		printf("Client disconnected!\n");
		return -1; // Error connection of client
	}

	perHandleData->func = -1;
	return 53;
}


/**
*	@desc:					List files in a directory
*	@param	path			path of directory
*   @param  output			Contains file list in byte string format
*/
void ListFile(string path, char* output) {
	stringstream ss;

	// 	Browse the directory given by path
	// WARM: a subdirectory will have afterfix /
	// Eg: arduino.zip		image/		demo.txt
	for (const auto & file : fs::directory_iterator(path)) {

		ss << "\t" << file.path().filename();

		if (file.path().has_extension()) { // is a file
			ss << "\n";
		}
		else { // is a  sub-folder
			ss << "/\n";
		}
	}

	ss << "  " << '\0';
	ss.read(output, BUFF_SIZE);
}


/**
*	@desc:					Handles a request to view a directory's file list
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
int LsHandler(LPPER_HANDLE_DATA perHandleData, char msg[]) {
	if (perHandleData->user == "") { // not login
		return 91;
	}

	string path = "./Server/" + perHandleData->user;
	path.append(msg);

	// TEST:
	cout << path << endl;


	//	if (path doen't exits) then return 72
	if (fs::is_directory(path) == false) {
		return 72;
	}

	char sBuff[BUFF_SIZE];
	ListFile(path, sBuff);

	// TEST:
	cout << "LIST FILE: \n";
	cout << sBuff << endl;

	if (send(perHandleData->socket, sBuff, strlen(sBuff), 0) < 0) {
		return -1;
	}

	return 71;
}

/**
*	@desc:					create directory given by path
*	@param	path			path
*/
int MakeDirectory(string path) {

	if (_mkdir(path.c_str()) == 0) {
		printf("Directory '%s' was successfully created\n", path.c_str());
		return 0;
	}
	else {
		printf("Make directory failed!\n");
	}

	return -1;
}


/**
*	@desc:					Handles a request to create sub-folder
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
int MkdirHandler(LPPER_HANDLE_DATA perHandleData, char msg[]) {
	if (perHandleData->user == "") { // not login
		return 91;
	}

	string path = "./Server/" + perHandleData->user;
	path.append(msg);

	// TEST:
	cout << path << endl;


	//	if path exits
	if (fs::is_directory(path)) {
		return 63;
	}

	if (MakeDirectory(path) < 0) { // Invalid path
		return 62;
	}

	return 61;
}

/**
*	@desc:					write the data the client sends to the file
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
void TransferFile(LPPER_HANDLE_DATA perHandleData, char data[]) {

	fstream& file = perHandleData->file;

	if (perHandleData->func == UPLOAD) {
		file.write(data, perHandleData->transferBytes);

		int totalBytesRevc = file.tellg();
		if (totalBytesRevc == perHandleData->bytes) {
			file.close();
			send(perHandleData->socket, "43", 2, 0);
			perHandleData->func = -1;
			printf("Get the file complete!\n");
		}
	}

	if (perHandleData->func == DOWNLOAD) {
		// TODO: 
	}
}


// Update new account information in database
int WriteAccount(string user, string pwd) {

	EnterCriticalSection(&criticalSection);
	ofstream ofs;
	ofs.open("./Database/account.txt", ios_base::app | ios_base::out);

	if (!ofs.good()) {
		printf("Ghi tai khoan that bai\n");
		return -1;
	}

	ofs << user << " " << pwd << endl;
	ofs.close();

	// Update database for app
	accountData.insert({
		user,
		{ pwd, /*logged = */ false }
	});

	LeaveCriticalSection(&criticalSection);
	return 0;
}

/**
*	@desc:					handling account registration messages
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
/* data == "username password confirm" */
int RegisterHandler(LPPER_HANDLE_DATA perHandleData, char data[]) {
	stringstream ss(data);

	string username;
	string password;
	string confirmPass;
	ss >> username >> password >> confirmPass;

	// Thieu thong tin
	if (username == "" || password == "" || confirmPass == "") {
		return 13;
	}

	// Pass again incorrect!
	if (password != confirmPass) {
		return 14;
	}

	// confirm username already exists
	bool accountExist;

	EnterCriticalSection(&criticalSection);
	auto it = accountData.find(username);
	if (it == accountData.end()) {
		accountExist = false;
	}
	else {
		accountExist = true;
	}
	LeaveCriticalSection(&criticalSection);

	if (accountExist) return 12;

	// Create new account
	if (MakeDirectory("./Server/" + username) < 0) {
		return 99; // server error
	}
	if (WriteAccount(username, password) < 0) {
		return 99; // server error
	}

	return 11;
}

/**
*	@desc:					handle login messages
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
int LoginHandler(LPPER_HANDLE_DATA perHandleData, char data[]) {
	if (perHandleData->user != "") { // Logged in
		return 23;
	}

	// data: user  pwd
	stringstream ss(data);
	string user;
	string pwd;
	ss >> user >> pwd;

	bool accountExist = false;
	bool accountLogin = false;
	bool accountVerify = false;

	EnterCriticalSection(&criticalSection);
	auto it = accountData.find(user);
	if (it == accountData.end()) {
		accountExist = false;
	}
	else {
		accountExist = true;
		accountLogin =  /*?logged = */ it->second.second;
		accountVerify = /*password = */it->second.first == pwd;
	}
	LeaveCriticalSection(&criticalSection);

	// When client login a account, 
	// cannot login another difference accout
	if (accountLogin) { // Session is active
		return 92;
	}

	if (!accountExist || !accountVerify) { // wrong information
		return 22;
	}

	// Login successfully
	perHandleData->user = user;
	EnterCriticalSection(&criticalSection);
	accountData[user].second = true; // active session
	LeaveCriticalSection(&criticalSection);

	return 21;
}

/**
*	@desc:					handle logout messages
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
int LogoutHanlder(LPPER_HANDLE_DATA perHandleData) {
	if (perHandleData->user == "") {
		return 91; // not login
	}

	string user = perHandleData->user;

	EnterCriticalSection(&criticalSection);
	accountData[user].second = false;
	LeaveCriticalSection(&criticalSection);

	perHandleData->user = "";
	return 31;
}

/**
*	@desc:					handle delete folder messages
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
int DeleteHandler(LPPER_HANDLE_DATA perHandleData, char msg[]) {
	if (perHandleData->user == "") {
		return 91; // not login
	}


	string path = msg;
	path = "./Server/" + perHandleData->user + path;

	if (fs::path(path).has_extension()) {


		fstream file(path);
		if (!file.good()) { // file doesn't exist
			return 82;
		}
		file.close();

		// Delete File
		if (remove(path.c_str()) == 0) {
			printf("Delete file '%s'", path.c_str());
		}
		else {
			printf("Delete fail");
		}

	}
	else {
		if (!fs::is_directory(path)) {
			return 82;
		}

		// Delete folder
		fs::remove_all(path);
	}

	return 81;
}

/**
*	@desc:					handle  messages
*	@param	perHandleData	socket identifier on completion port
*   @param  msg				data
*	@return					response code
*/
int handle(LPPER_HANDLE_DATA perHandleData, char msg[]) {
	if (perHandleData->func < 0) {
		msg[perHandleData->transferBytes] = 0;
	}
	else {
		TransferFile(perHandleData, msg);
		return -1;
	}

	string request = msg;
	string type = getType(request);
	int responseCode;

	// route path
	if (type == "CREATE") {
		responseCode = RegisterHandler(perHandleData, msg + strlen("CREATE "));
	}
	else if (type == "USER") {
		responseCode = LoginHandler(perHandleData, msg + strlen("USER "));
	}
	else if (type == "BYE") {
		responseCode = LogoutHanlder(perHandleData);
	}
	else if (type == "PUT") {
		responseCode = PutHandler(perHandleData, msg + strlen("PUT "));
	}
	else if (type == "GET") {
		responseCode = GetHandler(perHandleData, msg + strlen("GET "));
	}
	else if (type == "LS") {
		responseCode = LsHandler(perHandleData, msg + strlen("LS "));
	}
	else if (type == "MKDIR") {
		responseCode = MkdirHandler(perHandleData, msg + strlen("MKDIR "));
	}
	else if (type == "DELETE") {
		responseCode = DeleteHandler(perHandleData, msg + strlen("DELETE "));
	}
	else {
		responseCode = 93; // Syntax error
	}


	return responseCode;
}

void	EventHandle(LPPER_HANDLE_DATA perHandleData, LPPER_IO_OPERATION_DATA perIoData) {

	char* rBuff = perIoData->dataBuff.buf;


	cout << "Number of bytes received : " << perIoData->bufLen << endl;

	/* Stream processing -> handle request*/
	int responseCode = handle(perHandleData, rBuff);
	if (responseCode == -1) {
		return;
	}

	/* Respond to client*/
	// Covert int to character
	char sBuff[3];
	sBuff[0] = '0' + responseCode / 10;
	sBuff[1] = '0' + responseCode % 10;

	send(perHandleData->socket, sBuff, 2, 0);

	return;
}

unsigned __stdcall threadWorker(LPVOID completionPortID) {
	HANDLE completionPort = (HANDLE)completionPortID;
	DWORD transferredBytes;
	LPPER_HANDLE_DATA perHandleData;
	LPPER_IO_OPERATION_DATA perIoData;
	DWORD flags;

	while (1) {
		GetQueuedCompletionStatus(completionPort, &transferredBytes, (LPDWORD)&perHandleData, (LPOVERLAPPED *)&perIoData, INFINITE);

		// Error, then close socket
		if (transferredBytes == 0) {
			// Release socket and session

			EnterCriticalSection(&criticalSection);
			string user = perHandleData->user;
			if (user != "") {
				accountData[user].second = false;
			}
			LeaveCriticalSection(&criticalSection);

			closesocket(perHandleData->socket);
			delete perHandleData;
			delete perIoData;
			printf("Client disconnected!\n");
			continue;
		}

		if (perIoData->operation == RECEIVE) {
			perIoData->bufLen = transferredBytes;
			perHandleData->transferBytes = transferredBytes;

			EventHandle(perHandleData, perIoData);

			if (perHandleData == NULL) {
				delete perIoData;
				return 0;
			}

			memset(&(perIoData->overlapped), 0, sizeof(OVERLAPPED));
			perIoData->dataBuff.buf = perIoData->buffer;
			perIoData->dataBuff.len = BUFF_SIZE;
			flags = 0;
			WSARecv(perHandleData->socket,
				&(perIoData->dataBuff),
				1,
				&transferredBytes,
				&flags,
				&(perIoData->overlapped), NULL);
		}
	}

	return 0;
}


int main()
{
	LPPER_HANDLE_DATA perHandleData;
	LPPER_IO_OPERATION_DATA perIoData;
	DWORD transferredBytes;
	DWORD flags;
	SOCKET listenSock, acceptSock;

	WSAData wsaData;
	if (WSAStartup((2, 2), &wsaData) != 0) {
		printf("WSAStartup() failed with error %d\n", GetLastError());
		return 1;
	}

	/* Create completion port */
	HANDLE completionPort;
	completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	/* Tao theads cho completion port packet */
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);

	for (int i = 0; i < (int)systemInfo.dwNumberOfProcessors * 2; i++) {
		_beginthreadex(0, 0, threadWorker, (void*)completionPort, 0, 0);
	}

	listenSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	inet_pton(AF_INET, ip, &serverAddr.sin_addr);
	if (bind(listenSock, (PSOCKADDR)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		printf("bind() failed with error %d\n", WSAGetLastError());
		return 1;
	}

	// Prepare socket for listening
	if (listen(listenSock, maxClients) == SOCKET_ERROR) {
		printf("listen() failed with error %d\n", WSAGetLastError());
		return 1;
	}

	printf("Server started!\n");

	InitializeCriticalSection(&criticalSection);
	LoadAccount();

	while (1) {
		acceptSock = WSAAccept(listenSock, NULL, NULL, NULL, 0);
		if (acceptSock == SOCKET_ERROR) {
			printf("WSAAccept() failed with error %d\n", WSAGetLastError());
			return 1;
		}

		perHandleData = new PER_HANDLE_DATA;
		perHandleData->socket = acceptSock;
		perHandleData->user = "";
		perHandleData->func = -1;
		/* Associate socket with completion port */
		CreateIoCompletionPort((HANDLE)acceptSock, completionPort, (DWORD)perHandleData, 0);


		perIoData = new PER_IO_OPERATION_DATA;
		memset(&(perIoData->overlapped), 0, sizeof(OVERLAPPED));
		strcpy(perIoData->archive, "");
		perIoData->dataBuff.len = BUFF_SIZE;
		perIoData->dataBuff.buf = perIoData->buffer;
		perIoData->operation = RECEIVE;
		flags = 0;

		WSARecv(acceptSock, &(perIoData->dataBuff), 1, &transferredBytes, &flags, &(perIoData->overlapped), NULL);
	}

	DeleteCriticalSection(&criticalSection);
	closesocket(listenSock);
	WSACleanup();
	return 0;
}

