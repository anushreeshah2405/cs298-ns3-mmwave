#ifndef CANDIDATEBASESTATIONS_H
#define CANDIDATEBASESTATIONS_H

#include <vector>

class CandidateBaseStations {
public:
    static std::vector<int> stationsList;

    static std::vector<int> getStationsList() {
        return stationsList;
    }

    static void setStationsList(const std::vector<int>& newList) {
        stationsList = newList;
    }

private:
    CandidateBaseStations(); // Prevent instantiation
};

#endif // CANDIDATEBASESTATIONS_H
