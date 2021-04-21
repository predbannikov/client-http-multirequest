/*
Не известно есть ли требования к архитектуре(сколько памяти, какой процессор,
ширина канала), но разрешено использовать WinAPI.
По этому делаю реализацию основной идеи,
предполагая что используется стандартная система имеющая для программ хотя-бы
несколько мегабайт памяти.

Исходя из известной информации описанной в тестовом задании,
нет требования на последовательность запросов, но нужно получить больше ответов,
по этому делаю множественные запросы.
Нет требования на скорость обработки - данные упаковываю в std::vector некоторого
размера и по достижению максимума передаю в общую память (SharedMemory->writeData)
указатель на упакованные данные без блокировки мьютексами.


Для реализации использую функцию select. Для дальнейшего сопровождения кода,
необходимо сделать наличие обработки ошибок сокетов протестировать на другие виды
ошибок. А так же я бы провёл тесты, на скорость по отношению к другим методам
асинхронных запросов.
После окончательного решения к применению алгоритма, взялся бы
за рефакторинг/оптимизацию кода.

Надо добавить, что количество сокетов в этом варианте ограничено,
но при этом и не известна ширина канала и количество памяти.
Максимум сокетов можно увеличить, изменив текущую реализацию. Задать отправку
максимального количества запросов и изменить построение запросов на неблокирующий
режим.

Есть ещё множество моментов для улучшения, но думаю для тестового задания оставить как есть


Для общих данных используется кольцевой буфер std::vector<std::vector<char *> *>
Указатели полученных данных с сокетов собираются в пакет из PACK_SIZE_RESPONSE элементов
В общую память, после заполнения пакета std::vector<char *> (из PACK_SIZE_RESPONSE элементов)
передаётся ссылка на этот вектор.

В такой реализации возможна ситуация когда монитор не успевает обрабатывать данные,
в этом случае workThread (поток запросов) входит в блокировку с 10мс задержкой

*/

#include <iostream>
#include <winsock.h>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include "urls.h"

#pragma comment(lib, "ws2_32.lib")

#define MAX_PACKET_SIZE		65535		// Размер буфера для чтения recv
#define TIMEOUT_REQ			5000000		// Таймаут	(не использую)
#define PORT_CONNECTION		80			// Порт подключения
#define PACK_SIZE_RESPONSE	16			// Размер пакета на чтение
#define COUNT_REST_REQUEST	16			// Количество запросов
#define MAX_SIZE_SHARE_BUF	8			// Размер кольцевого буфера


// 1 - только 127.0.0.1	| 0 - рандомно из списка		в файле urls.dat первой строкой должен быть этот адрес
#define	ONLYLOCALHOST	0

class SharedMemory {
	std::mutex mtx;
	std::vector<std::vector<char *> *> data;
	//bool wait;

	// нужно быть уверенным, что индексы при обращении к вектору не совпадают
	std::atomic<int> index1;
	std::atomic<int> index2;
	void checkIndex1() {
		++index1;
		if (index1 == MAX_SIZE_SHARE_BUF)
			index1 = 0;
		//if (index1 == index2)
		//	wait = true;
		//else
		//	wait = false;
	}
	void checkIndex2() {
		++index2;
		if (index2 == MAX_SIZE_SHARE_BUF)
			index2 = 0;
	}
public:
	bool cycle;		// true Основной цикл запросов

	SharedMemory() : // wait(false), 
		cycle(true) {
		index1 = 0;
		index2 = 0;
		data.resize(MAX_SIZE_SHARE_BUF);
		for (int i = 0; i < MAX_SIZE_SHARE_BUF; i++)
			data[i] = NULL;
	}

	~SharedMemory() {
		for (size_t i = 0; i < data.size(); i++) {
			if (data[i]) {
				for (size_t j = 0; j < data[i]->size(); j++) {
					if (data[i]->at(j))
						free(data[i]->at(j));
				}
			}
		}
	}

	/*
	Чтение данных,
	индекс сдвигается только в случае если index1 ещё не догнал index2, если такое случилось
	то поток запросов входит в блокировку и ожидает NULL
	*/
	bool readData(std::vector<char *> *&a_aData) {

		if (data[index1] != NULL) {
			std::cout << "Pack index: " << index1 << " id = " << std::this_thread::get_id() << std::endl;

			//// Для теста, визуально просмотреть заполнение буфера
			//for (int i = 0; i < MAX_SIZE_SHARE_BUF; i++) {
			//	std::cout << i << "=" << data[i] << " " << std::endl;
			//}

			a_aData = data[index1];
			data[index1] = NULL;
			// **********************************************************************************************************************
			//std::this_thread::sleep_for(std::chrono::milliseconds(1000));	// для теста, монитор не успевает обрабатывать данные
			// **********************************************************************************************************************
			checkIndex1();
		}
		else {
			return 1;
		}
		return 0;
	}
	/*
	Запись данных в буфер после каждого обращения index2 сдвигается вперёд
	*/
	void writeData(std::vector<char *> *a_aData) {

		while (data[index2] != NULL) { // Если память не свободна ждём 10 мс
			std::cout << "not equal NULL id = " << std::this_thread::get_id() << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (!cycle)
				break;
			// Сюда можно добавить увеличение буфера, но если монитор не успевает обработать данные 
			// то надо искать другое решение или задуматься о производительности кода
		}
		data[index2] = a_aData;
		checkIndex2();
	}

};


struct Request {
	char *data = NULL;
	std::string sSrcREST;
	std::string sReqHTTP;
	std::string sServer;
	std::string sPath;
	std::string sFileName;
	SOCKADDR_IN sockaddr_in;
	SOCKET		sock;
	int totalLength = 0;
	bool done = false;
};

void workThread();

int countRequest;

std::map<SOCKET, Request> mapRequests;

std::vector<char*> *g_vec; // (PACK_SIZE_RESPONSE, NULL);

fd_set  fdread;

SharedMemory *shareMem;

Urls url;



/*
	Разобрать строку запроса на части
*/
void parseUrl(const char *mUrl, std::string &serverName, std::string &filepath, std::string &filename)
{
	std::string::size_type n;
	std::string url = mUrl;
	if (url.substr(0, 7) == "http://")
		url.erase(0, 7);

	if (url.substr(0, 8) == "https://")
		url.erase(0, 8);

	n = url.find('/');
	if (n != std::string::npos)
	{
		serverName = url.substr(0, n);
		filepath = url.substr(n);
		n = filepath.rfind('/');
		filename = filepath.substr(n + 1);
	}
	else {
		serverName = url;
		filepath = "/";
		filename = "";
	}
}

void printError(int anError)
{
	wchar_t *s = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, anError,
		MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
		(LPWSTR)&s, 0, NULL);
	fprintf(stderr, "error: %S\n", s);
	LocalFree(s);
}

/*
	Создать сокет, настроить запрос
*/
int setRequest(std::string a_sSrcREST, Request &req)
{
	SOCKET sock;
	req.sSrcREST = a_sSrcREST;
	parseUrl(a_sSrcREST.c_str(), req.sServer, req.sPath, req.sFileName);
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET) {
		std::cout << "create socket failed with INVALID_SOCKET" << std::endl;
		return WSAGetLastError();;
	}
	req.sock = sock;
	struct hostent *hp;
	unsigned int addr;

	const char *serverName = (char *)req.sServer.c_str();
	if (inet_addr(serverName) == INADDR_NONE)
	{
		hp = gethostbyname(serverName);
	}
	else
	{
		addr = inet_addr(serverName);
		hp = gethostbyaddr((char*)&addr, sizeof(addr), AF_INET);

	}

	if (hp == NULL)
	{
		std::cout << "get ip address host failed " << std::endl;
		//closesocket(sock);
		return WSAGetLastError();
	}
	SOCKADDR_IN internetAddr;
	internetAddr.sin_family = AF_INET;
	internetAddr.sin_port = htons(PORT_CONNECTION);
	internetAddr.sin_addr.s_addr = *((unsigned long*)hp->h_addr);
	req.sockaddr_in = internetAddr;

	return 0;
}

/*
	Подключиться и выполнить запрос
*/
int execRequest(Request &req)
{
	if (connect(req.sock, (struct sockaddr*) &req.sockaddr_in, sizeof(req.sockaddr_in)))
	{
		std::cout << "connect failed with error " << std::endl;
		return WSAGetLastError();
	}

	req.sReqHTTP = "GET " + req.sPath + " HTTP/1.0\r\n" + "Host: " + req.sServer + " \r\n\r\n";

	if (SOCKET_ERROR == send(req.sock, req.sReqHTTP.c_str(), (int)strlen(req.sReqHTTP.c_str()), 0))
	{
		std::cout << "send failed with error" << std::endl;
		return WSAGetLastError();
	}
	return 0;
}

// Переделать на конечный автомат
int createRequest()
{
	std::string srcREST;
	if (ONLYLOCALHOST)
		srcREST = url.at(0);
	else
		srcREST = url.getSrcRNDRESTRequest();

	Request req;
	int ret = 0;
	ret = setRequest(srcREST, req);
	if (!ret) {
		ret = execRequest(req);
		if (!ret) {
			mapRequests.insert(std::pair<SOCKET, Request>(req.sock, req));
			return 0;
		}
		else {
			printError(ret);
			closesocket(req.sock);
		}
	}
	else {
		printError(ret);
		closesocket(req.sock);
	}
	return 1;
}


/*
	обработать карту запросов
*/
void procMapRequests() {

	// переделать поиск и удаление на лямбду 
	for (auto it = mapRequests.begin(); it != mapRequests.end(); ++it) {
		if (it->second.done) {
			//std::cout << "change request" << std::endl;
			//std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			closesocket(it->second.sock);
			mapRequests.erase(it);
			break;
		}
	}


	while (mapRequests.size() < COUNT_REST_REQUEST) {
		if (createRequest())
			std::cout << "don't create request" << std::endl;// Нужно что-то для контроля, если не будет удаваться создать!!!
	}


}


/*
	Получить данные из сокета
*/
int readSock() {
	char		buff[MAX_PACKET_SIZE];
	for (auto it = mapRequests.begin(); it != mapRequests.end(); ++it) {
		SOCKET sock = it->first;
		if (FD_ISSET(sock, &fdread))
		{
			int len;
			if (SOCKET_ERROR == (len = recv(sock, (char *)&buff, MAX_PACKET_SIZE, 0)))
			{
				// Возможно можно выбросить этот url из запросов
				return WSAGetLastError();
			}
			//printf("\nsock = %d | len = %d | sizeof(%d)\n", sock, len, sizeof(buff));
			//printf("REST: %s", it->second.sSrcREST.c_str());
			if (len == 0) {
				it->second.data = (char*)realloc(it->second.data, len + it->second.totalLength);		// Нужна проверка результата на NULL 
				memcpy(it->second.data + it->second.totalLength, buff, len);
				it->second.totalLength += len;

				it->second.data = (char*)realloc(it->second.data, 1 + it->second.totalLength);		// Нужна проверка результата на NULL 
				memcpy(it->second.data + it->second.totalLength, "\0", 1);


				it->second.done = true;

				//printf("\n%s\nend of file\n", it->second.data);
				g_vec->at(countRequest) = it->second.data;
				countRequest++;
				procMapRequests();
				return 0;
			}
			it->second.data = (char*)realloc(it->second.data, len + it->second.totalLength);		// Нужна проверка результата на NULL 
			memcpy(it->second.data + it->second.totalLength, buff, len);
			it->second.totalLength += len;
		}
	}
	return 0;
}

/*
	Раобочий поток выполнения запросов
*/
void workThread()
{
	std::cout << "start workThread thread: " << std::this_thread::get_id() << std::endl;

	char *memBuffer = NULL;

	WSADATA wsaData;
	int ret = 0;
	if ((ret = WSAStartup(MAKEWORD(1, 1), &wsaData)) != 0)
	{
		printf("WSAStartup failed with error %d \n", ret);
		return;
	}
	procMapRequests();

	timeval	time_out;
	time_out.tv_sec = 0;
	time_out.tv_usec = TIMEOUT_REQ;

	g_vec = new std::vector<char *>(PACK_SIZE_RESPONSE, NULL);

	while (shareMem->cycle)
	{

		FD_ZERO(&fdread);


		for (auto it = mapRequests.begin(); it != mapRequests.end(); ++it) {
			if (it->second.done) {

				continue;
			}
			FD_SET(it->first, &fdread);
		}
		if (!fdread.fd_count) {
			std::cout << "fdread empty error" << std::endl;
			break;
		}




		ret = 0;

		//if ((ret = select(0, &fdread, NULL, NULL, &time_out)) == SOCKET_ERROR)
		if ((ret = select(0, &fdread, NULL, NULL, NULL)) == SOCKET_ERROR)
		{
			std::cout << "error stop";
			continue;
		}
		if (ret != 0)
		{
			ret = readSock();
			if (ret) {
				printError(ret);
				continue;
			}
		}
		if (countRequest == PACK_SIZE_RESPONSE) {
			countRequest = 0;
			shareMem->writeData(g_vec);
			g_vec = new std::vector<char *>(PACK_SIZE_RESPONSE, NULL);
		}

	}


	if (WSACleanup() == SOCKET_ERROR) {
		printError(WSAGetLastError());
	}
}


int main()
{
	std::cout << "start main thread: " << std::this_thread::get_id() << std::endl;

	shareMem = new SharedMemory();

	auto thr = std::thread(workThread);

	std::vector<char *> *pVec = NULL;
	int counterCurPosPack = 0;

	while (GetAsyncKeyState(VK_ESCAPE) == 0) {
		if (counterCurPosPack == PACK_SIZE_RESPONSE) {
			counterCurPosPack = 0;
			delete pVec;			
		}
		if (counterCurPosPack == 0) {
			if (shareMem->readData(pVec)) {
				//std::cout << "Wait when will buff data filled" << std::endl;

										// Если буфер пуст то можно и подождать 
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}
		}
		if (pVec == NULL) {
			std::cout << "stop";
			break;
		}
		printf("%s", pVec->at(counterCurPosPack));
		free(pVec->at(counterCurPosPack));
		counterCurPosPack++;
	}

	std::cout << std::endl << "exiting, pleas wait close all threads" << std::endl;
	shareMem->cycle = false;
	thr.join();

	delete shareMem;

	return 0;
}
