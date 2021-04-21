#include <iostream>
#include <string>
#include <fstream>
#include <ctime>

class Urls {
	std::vector<std::string> m_sData;
public:
	Urls() {
		srand((unsigned)std::time(0));

		std::string s; 

		std::ifstream file("./urls.dat"); 

		while (getline(file, s)) { // ���� �� ��������� ����� ����� ������ ��������� ������ � ���������� (s)
			m_sData.push_back(s); // ������� �� �����
		}

		file.close(); // ����������� ��������� ���� ��� �� �� ��������� ���
	}


	/*
		�������� ��������� URL �� ������
	*/
	std::string getSrcRNDRESTRequest()
	{
		size_t countLink = m_sData.size();
		int r = rand() % countLink;
		return m_sData[r];
	}

	std::vector<std::string> *data() {
		return &m_sData;
	}

	size_t size() {
		return m_sData.size();
	}

	std::string at(size_t a_nIndex) {
		if (a_nIndex >= 0 && a_nIndex < m_sData.size()) {
			return m_sData[a_nIndex];
		} 
		return "localhost";
	}

};