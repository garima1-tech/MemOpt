#ifndef DATASET_H
#define DATASET_H

#include <string>
#include <vector>

class Dataset {
public:
    Dataset();

    bool loadDataset(const std::string& filePath);

    int getRowCount() const;

    std::vector<std::string> getData() const;

    void clearDataset();

private:
    std::vector<std::string> data;
};

#endif
