#pragma once

struct Record {
    std::string name;
    int score;
    double play_time;
};

class RetiredPlayersRepository {
public:
    explicit RetiredPlayersRepository(std::string db_url);

    void InitTable();

    void AddRecord(std::string name, int score, double play_time);

    std::vector<Record> GetRecords(int start, int max_items);
};