/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 * Copyright (c) 2013 Budiarto Herman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Original work authors (from lte-enb-rrc.cc):
 * - Nicola Baldo <nbaldo@cttc.es>
 * - Marco Miozzo <mmiozzo@cttc.es>
 * - Manuel Requena <manuel.requena@cttc.es>
 *
 * Converted to handover algorithm interface by:
 * - Budiarto Herman <budiarto.herman@magister.fi>
 */

#include <cmath>
#include <set>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <random>

#include "a2-a4-rsrq-handover-algorithm.h"

#include <ns3/log.h>
#include <ns3/uinteger.h>

#include "CandidateBaseStations.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("A2A4RsrqHandoverAlgorithm");

NS_OBJECT_ENSURE_REGISTERED(A2A4RsrqHandoverAlgorithm);

// vars
std::map<int, std::set<int>> printableData;
std::map<int, std::map<int, int>> inputData;
bool modelConventional = true;
int lockedUntilTime = -1;

///////////////////////////////////////////
// Handover Management SAP forwarder
///////////////////////////////////////////

A2A4RsrqHandoverAlgorithm::A2A4RsrqHandoverAlgorithm()
    : m_a2MeasId(0),
      m_a4MeasId(0),
      m_servingCellThreshold(30),
      m_neighbourCellOffset(1),
      m_handoverManagementSapUser(0)
{
    NS_LOG_FUNCTION(this);
    m_handoverManagementSapProvider =
        new MemberLteHandoverManagementSapProvider<A2A4RsrqHandoverAlgorithm>(this);
}

A2A4RsrqHandoverAlgorithm::~A2A4RsrqHandoverAlgorithm()
{
    NS_LOG_FUNCTION(this);
}

TypeId
A2A4RsrqHandoverAlgorithm::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::A2A4RsrqHandoverAlgorithm")
            .SetParent<LteHandoverAlgorithm>()
            .SetGroupName("Lte")
            .AddConstructor<A2A4RsrqHandoverAlgorithm>()
            .AddAttribute("ServingCellThreshold",
                          "If the RSRQ of the serving cell is worse than this "
                          "threshold, neighbour cells are consider for handover. "
                          "Expressed in quantized range of [0..34] as per Section "
                          "9.1.7 of 3GPP TS 36.133.",
                          UintegerValue(30),
                          MakeUintegerAccessor(&A2A4RsrqHandoverAlgorithm::m_servingCellThreshold),
                          MakeUintegerChecker<uint8_t>(0, 34))
            .AddAttribute("NeighbourCellOffset",
                          "Minimum offset between the serving and the best neighbour "
                          "cell to trigger the handover. Expressed in quantized "
                          "range of [0..34] as per Section 9.1.7 of 3GPP TS 36.133.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&A2A4RsrqHandoverAlgorithm::m_neighbourCellOffset),
                          MakeUintegerChecker<uint8_t>());
    return tid;
}

void
A2A4RsrqHandoverAlgorithm::SetLteHandoverManagementSapUser(LteHandoverManagementSapUser* s)
{
    NS_LOG_FUNCTION(this << s);
    m_handoverManagementSapUser = s;
}

LteHandoverManagementSapProvider*
A2A4RsrqHandoverAlgorithm::GetLteHandoverManagementSapProvider()
{
    NS_LOG_FUNCTION(this);
    return m_handoverManagementSapProvider;
}

void
A2A4RsrqHandoverAlgorithm::DoInitialize()
{
    NS_LOG_FUNCTION(this);

    NS_LOG_LOGIC(this << " requesting Event A2 measurements"
                      << " (threshold=" << (uint16_t)m_servingCellThreshold << ")");
    LteRrcSap::ReportConfigEutra reportConfigA2;
    reportConfigA2.eventId = LteRrcSap::ReportConfigEutra::EVENT_A2;
    reportConfigA2.threshold1.choice = LteRrcSap::ThresholdEutra::THRESHOLD_RSRQ;
    reportConfigA2.threshold1.range = m_servingCellThreshold;
    reportConfigA2.triggerQuantity = LteRrcSap::ReportConfigEutra::RSRQ;
    reportConfigA2.reportInterval = LteRrcSap::ReportConfigEutra::MS240;
    m_a2MeasId = m_handoverManagementSapUser->AddUeMeasReportConfigForHandover(reportConfigA2);

    NS_LOG_LOGIC(this << " requesting Event A4 measurements"
                      << " (threshold=0)");
    LteRrcSap::ReportConfigEutra reportConfigA4;
    reportConfigA4.eventId = LteRrcSap::ReportConfigEutra::EVENT_A4;
    reportConfigA4.threshold1.choice = LteRrcSap::ThresholdEutra::THRESHOLD_RSRQ;
    reportConfigA4.threshold1.range = 0; // intentionally very low threshold
    reportConfigA4.triggerQuantity = LteRrcSap::ReportConfigEutra::RSRQ;
    reportConfigA4.reportInterval = LteRrcSap::ReportConfigEutra::MS480;
    m_a4MeasId = m_handoverManagementSapUser->AddUeMeasReportConfigForHandover(reportConfigA4);

    LteHandoverAlgorithm::DoInitialize();
}

void
A2A4RsrqHandoverAlgorithm::DoDispose()
{
    NS_LOG_FUNCTION(this);
    delete m_handoverManagementSapProvider;
}

void
A2A4RsrqHandoverAlgorithm::DoReportUeMeas(uint16_t rnti, LteRrcSap::MeasResults measResults)
{
    NS_LOG_FUNCTION(this << rnti << (uint16_t)measResults.measId);

    if (measResults.measId == m_a2MeasId)
    {
        NS_ASSERT_MSG(measResults.rsrqResult <= m_servingCellThreshold,
                      "Invalid UE measurement report");
        EvaluateHandover(rnti, measResults.rsrqResult);
    }
    else if (measResults.measId == m_a4MeasId)
    {
        if (measResults.haveMeasResultNeighCells && !measResults.measResultListEutra.empty())
        {
            for (std::list<LteRrcSap::MeasResultEutra>::iterator it =
                     measResults.measResultListEutra.begin();
                 it != measResults.measResultListEutra.end();
                 ++it)
            {
                NS_ASSERT_MSG(it->haveRsrqResult == true,
                              "RSRQ measurement is missing from cellId " << it->physCellId);
                UpdateNeighbourMeasurements(rnti, it->physCellId, it->rsrqResult);
            }
        }
        else
        {
            NS_LOG_WARN(
                this << " Event A4 received without measurement results from neighbouring cells");
        }
    }
    else
    {
        NS_LOG_WARN("Ignoring measId " << (uint16_t)measResults.measId);
    }

} // end of DoReportUeMeas

// /Users/thegeekylad/Desktop/cs298/input/input-dwell-time.csv

std::map<int, std::map<int, int>> parseDwellTimeData()  {
    std::map<int, std::map<int, int>> printableData;

    std::ifstream inputFile("../../../data/input-dwell-time.csv");

    if (!inputFile.is_open()) {
        std::cerr << "Error opening the file." << std::endl;
        return printableData; // Return empty map if file cannot be opened
    }

    std::string line;
    std::string part;
    while (std::getline(inputFile, line)) {
        std::istringstream iss(line);

        std::getline(iss, part, ',');
        int t = std::stoi(part);

        std::getline(iss, part, ',');
        int baseStationId = std::stoi(part);

        std::getline(iss, part, ',');
        int dwellTime = static_cast<int>(round(std::stod(part) * 60));

        printableData[t][baseStationId] = dwellTime;
    }


    // Close the file
    inputFile.close();

    // for (const auto& pair : printableData) {
    //     int key = pair.first;
    //     const std::vector<std::pair<int, int>>& pairs = pair.second;

    //     for (const auto& p : pairs) {
    //         NS_LOG_WARN(key << "," << p.first << "," << p.second << std::endl);
    //     }
    // }

    return printableData;
}

void
logCandidateBaseStations()
{
    std::ofstream outputFile("data/log-candidate-base-stations.csv");

    if (!outputFile.is_open()) {
        NS_LOG_WARN("Error opening the file.");
        return;
    }

    for (const auto& pair : printableData) {
        std::pair<double, double> vehicleCoordinates = CandidateBaseStations::vehiclePositions[pair.first];
        for (int value : pair.second) {
            std::pair<double, double> baseStationCoordinates = CandidateBaseStations::stationsMap[value];
            int baseStationId = CandidateBaseStations::indexToIdStationsMap[value];
            outputFile << pair.first << "," << vehicleCoordinates.first << "," << vehicleCoordinates.second << "," << baseStationCoordinates.first << "," << baseStationCoordinates.second << "," << baseStationId << std::endl;
        }
    }

    outputFile.close();
    // std::cout << "Data written to the file successfully." << std::endl;
}

void
logHandovers(uint16_t cellId)
{
    std::ofstream outputFile("data/log-handovers.txt", std::ios::app);

    if (!outputFile.is_open()) {
        NS_LOG_WARN("Error opening the file.");
        return;
    }

    outputFile << cellId << std::endl;

    outputFile.close();
    // std::cout << "Data written to the file successfully." << std::endl;
}

int getRandomNumber(int min, int max) {
    std::random_device rd;
    
    std::mt19937 gen(rd());
    
    std::uniform_int_distribution<int> dist(min, max);
    
    return dist(gen);
}

void
A2A4RsrqHandoverAlgorithm::EvaluateHandover(uint16_t rnti, uint8_t servingCellRsrq)
{
    int currTime = round(Simulator::Now().GetSeconds() / CandidateBaseStations::timeDifference) * CandidateBaseStations::timeDifference;

    // read input dwell time data
    if (!modelConventional && inputData.size() == 0) {
        inputData = parseDwellTimeData();
        NS_LOG_WARN("Parsed dwell time data!");
    }

    NS_LOG_FUNCTION(this << rnti << (uint16_t)servingCellRsrq);

    MeasurementTable_t::iterator it1;
    it1 = m_neighbourCellMeasures.find(rnti);

    if (it1 == m_neighbourCellMeasures.end())
    {
        NS_LOG_WARN("Skipping handover evaluation for RNTI "
                    << rnti << " because neighbour cells information is not found");
    }
    else
    {
        // Find the best neighbour cell (eNB)
        NS_LOG_LOGIC("Number of neighbour cells = " << it1->second.size());
        uint16_t bestNeighbourCellId = 0;
        uint8_t bestNeighbourRsrq = 0;
        // int bestDwellTime = 0;
        MeasurementRow_t::iterator it2;

        // for each neighbor of this cell
        for (it2 = it1->second.begin(); it2 != it1->second.end(); ++it2)
        {
            int baseStationId = CandidateBaseStations::indexToIdStationsMap[it2->first];
            int dwellTime = inputData[currTime][baseStationId];

            if (dwellTime == 0) {
                inputData[currTime][baseStationId] = dwellTime = getRandomNumber(10, 20);
            }

            // NS_LOG_WARN("Current time: " << currTime);
            printableData[currTime].insert(it2->first);

            logCandidateBaseStations();
            NS_LOG_WARN("t = " << currTime << "s");
            NS_LOG_WARN("Neighbor: " << it2->first);
            NS_LOG_WARN("Dwell time: " << dwellTime << "s");
            NS_LOG_WARN("");

            // if ((it2->second->m_rsrq > bestNeighbourRsrq && (!modelConventional ? dwellTime > bestDwellTime : true)) && IsValidNeighbour(it2->first))
            if ((it2->second->m_rsrq > bestNeighbourRsrq) && IsValidNeighbour(it2->first))
            {
                bestNeighbourCellId = it2->first;
                bestNeighbourRsrq = it2->second->m_rsrq;
                // bestDwellTime = dwellTime;
            }
        }

        // Trigger Handover, if needed
        if (bestNeighbourCellId > 0)
        {
            NS_LOG_LOGIC("Best neighbour cellId " << bestNeighbourCellId);

            if ((bestNeighbourRsrq - servingCellRsrq) >= m_neighbourCellOffset)
            {
                if (currTime < lockedUntilTime)
                {
                    NS_LOG_WARN("Skipping handover: Service cell has enough dwell time");
                }
                else
                {
                    logHandovers(bestNeighbourCellId);
                    NS_LOG_WARN("Trigger Handover to cellId " << bestNeighbourCellId);
                    NS_LOG_LOGIC("target cell RSRQ " << (uint16_t)bestNeighbourRsrq);
                    NS_LOG_LOGIC("serving cell RSRQ " << (uint16_t)servingCellRsrq);

                    // get this cell's dwell time and add to "t"
                    if (!modelConventional) {
                        lockedUntilTime = currTime + inputData[currTime][CandidateBaseStations::indexToIdStationsMap[bestNeighbourCellId]];
                        NS_LOG_WARN("Latched on until t = " << lockedUntilTime << "s");
                    }

                    // Inform eNodeB RRC about handover
                    m_handoverManagementSapUser->TriggerHandover(rnti, bestNeighbourCellId);
                }
            }
        }

    } // end of else of if (it1 == m_neighbourCellMeasures.end ())

} // end of EvaluateMeasurementReport

bool
A2A4RsrqHandoverAlgorithm::IsValidNeighbour(uint16_t cellId)
{
    NS_LOG_FUNCTION(this << cellId);

    /**
     * \todo In the future, this function can be expanded to validate whether the
     *       neighbour cell is a valid target cell, e.g., taking into account the
     *       NRT in ANR and whether it is a CSG cell with closed access.
     */

    return true;
}

void
A2A4RsrqHandoverAlgorithm::UpdateNeighbourMeasurements(uint16_t rnti, uint16_t cellId, uint8_t rsrq)
{
    NS_LOG_FUNCTION(this << rnti << cellId << (uint16_t)rsrq);
    MeasurementTable_t::iterator it1;
    it1 = m_neighbourCellMeasures.find(rnti);

    if (it1 == m_neighbourCellMeasures.end())
    {
        // insert a new UE entry
        MeasurementRow_t row;
        std::pair<MeasurementTable_t::iterator, bool> ret;
        ret = m_neighbourCellMeasures.insert(std::pair<uint16_t, MeasurementRow_t>(rnti, row));
        NS_ASSERT(ret.second);
        it1 = ret.first;
    }

    NS_ASSERT(it1 != m_neighbourCellMeasures.end());
    Ptr<UeMeasure> neighbourCellMeasures;
    std::map<uint16_t, Ptr<UeMeasure>>::iterator it2;
    it2 = it1->second.find(cellId);

    if (it2 != it1->second.end())
    {
        neighbourCellMeasures = it2->second;
        neighbourCellMeasures->m_cellId = cellId;
        neighbourCellMeasures->m_rsrp = 0;
        neighbourCellMeasures->m_rsrq = rsrq;
    }
    else
    {
        // insert a new cell entry
        neighbourCellMeasures = Create<UeMeasure>();
        neighbourCellMeasures->m_cellId = cellId;
        neighbourCellMeasures->m_rsrp = 0;
        neighbourCellMeasures->m_rsrq = rsrq;
        it1->second[cellId] = neighbourCellMeasures;
    }

} // end of UpdateNeighbourMeasurements


} // end of namespace ns3
