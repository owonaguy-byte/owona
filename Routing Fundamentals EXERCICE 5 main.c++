/*
 * Exercice 5: Policy-Based Routing pour Application-Aware WAN Path Selection
 * 
 * MediaStream Inc. - Simulation NS-3
 * Implémentation complète avec classification de trafic, PBR et contrôleur SD-WAN
 * 
 * Compilation:
 * ./waf configure --enable-examples
 * ./waf build
 * 
 * Exécution:
 * ./waf --run scratch/pbr-simulation
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include <map>
#include <iostream>
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PbrSimulation");

// ========================================
// ÉNUMÉRATIONS ET STRUCTURES
// ========================================

enum TrafficClass {
    VIDEO_TRAFFIC,
    DATA_TRAFFIC,
    DEFAULT_TRAFFIC
};

struct PolicyRule {
    double latencyThreshold;        // ms
    double bandwidthThreshold;      // Mbps
    uint32_t primaryInterface;
    uint32_t secondaryInterface;
    uint32_t currentInterface;
};

struct PathMetrics {
    double latency;                 // ms
    double bandwidth;               // Mbps
    uint32_t packetsSent;
    uint32_t packetsReceived;
    Time lastUpdateTime;
};

// ========================================
// CLASSE: PathMetricsMonitor
// ========================================

class PathMetricsMonitor : public Object {
private:
    std::map<uint32_t, Time> m_packetSendTimes;
    std::map<uint32_t, PathMetrics> m_interfaceMetrics;
    std::map<uint32_t, std::vector<double>> m_latencyHistory;
    Ptr<FlowMonitor> m_flowMonitor;
    Ptr<Ipv4FlowClassifier> m_classifier;
    
public:
    static TypeId GetTypeId() {
        static TypeId tid = TypeId("PathMetricsMonitor")
            .SetParent<Object>()
            .SetGroupName("Applications");
        return tid;
    }
    
    PathMetricsMonitor() {
        NS_LOG_FUNCTION(this);
    }
    
    virtual ~PathMetricsMonitor() {
        NS_LOG_FUNCTION(this);
    }
    
    void Initialize(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier) {
        m_flowMonitor = monitor;
        m_classifier = classifier;
        
        // Initialiser les métriques pour chaque interface
        for (uint32_t i = 0; i < 5; i++) {
            PathMetrics metrics;
            metrics.latency = 0.0;
            metrics.bandwidth = 0.0;
            metrics.packetsSent = 0;
            metrics.packetsReceived = 0;
            metrics.lastUpdateTime = Simulator::Now();
            m_interfaceMetrics[i] = metrics;
        }
    }
    
    void EnableLatencyTracking(Ptr<Node> node) {
        // Connecter aux traces Ipv4L3Protocol
        std::ostringstream oss;
        oss << "/NodeList/" << node->GetId() << "/$ns3::Ipv4L3Protocol/Tx";
        Config::Connect(oss.str(), MakeCallback(&PathMetricsMonitor::PacketSent, this));
    }
    
    void PacketSent(std::string context, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface) {
        uint32_t uid = packet->GetUid();
        m_packetSendTimes[uid] = Simulator::Now();
        m_interfaceMetrics[interface].packetsSent++;
    }
    
    void PacketReceived(Ptr<const Packet> packet, uint32_t interface) {
        uint32_t uid = packet->GetUid();
        if (m_packetSendTimes.find(uid) != m_packetSendTimes.end()) {
            Time latency = Simulator::Now() - m_packetSendTimes[uid];
            double latencyMs = latency.GetMilliSeconds();
            
            // Mettre à jour les métriques
            m_latencyHistory[interface].push_back(latencyMs);
            if (m_latencyHistory[interface].size() > 100) {
                m_latencyHistory[interface].erase(m_latencyHistory[interface].begin());
            }
            
            // Calculer la moyenne mobile
            double avgLatency = 0.0;
            for (double lat : m_latencyHistory[interface]) {
                avgLatency += lat;
            }
            avgLatency /= m_latencyHistory[interface].size();
            
            m_interfaceMetrics[interface].latency = avgLatency;
            m_interfaceMetrics[interface].packetsReceived++;
            m_interfaceMetrics[interface].lastUpdateTime = Simulator::Now();
            
            m_packetSendTimes.erase(uid);
        }
    }
    
    void UpdateBandwidthMetrics() {
        if (!m_flowMonitor) return;
        
        m_flowMonitor->CheckForLostPackets();
        std::map<FlowId, FlowMonitor::FlowStats> stats = m_flowMonitor->GetFlowStats();
        
        for (auto const& flow : stats) {
            if (flow.second.rxPackets == 0) continue;
            
            double timeWindow = flow.second.timeLastRxPacket.GetSeconds() - 
                               flow.second.timeFirstTxPacket.GetSeconds();
            
            if (timeWindow > 0) {
                double throughput = (flow.second.rxBytes * 8.0) / timeWindow / 1e6; // Mbps
                
                // Approximation: utiliser le premier flux pour chaque interface
                Ipv4FlowClassifier::FiveTuple tuple = m_classifier->FindFlow(flow.first);
                
                // Interface basée sur le port de destination (simplification)
                uint32_t interface = 1; // Par défaut
                if (tuple.destinationPort == 5004) {
                    interface = 1; // Interface pour trafic vidéo
                } else {
                    interface = 2; // Interface pour trafic data
                }
                
                m_interfaceMetrics[interface].bandwidth = throughput;
            }
        }
        
        // Replanifier
        Simulator::Schedule(Seconds(1.0), &PathMetricsMonitor::UpdateBandwidthMetrics, this);
    }
    
    PathMetrics GetInterfaceMetrics(uint32_t interface) {
        if (m_interfaceMetrics.find(interface) != m_interfaceMetrics.end()) {
            return m_interfaceMetrics[interface];
        }
        return PathMetrics();
    }
    
    double GetInterfaceLatency(uint32_t interface) {
        return m_interfaceMetrics[interface].latency;
    }
    
    double GetInterfaceBandwidth(uint32_t interface) {
        return m_interfaceMetrics[interface].bandwidth;
    }
    
    void PrintMetrics() {
        std::cout << "\n========== MÉTRIQUES DES CHEMINS ==========\n";
        for (auto& metric : m_interfaceMetrics) {
            std::cout << "Interface " << metric.first << ":\n";
            std::cout << "  Latence: " << metric.second.latency << " ms\n";
            std::cout << "  Bande passante: " << metric.second.bandwidth << " Mbps\n";
            std::cout << "  Paquets envoyés: " << metric.second.packetsSent << "\n";
            std::cout << "  Paquets reçus: " << metric.second.packetsReceived << "\n";
        }
        std::cout << "==========================================\n\n";
    }
};

// ========================================
// CLASSE: PolicyBasedRouter
// ========================================

class PolicyBasedRouter : public Object {
private:
    Ptr<Node> m_routerNode;
    std::map<uint16_t, TrafficClass> m_portClassification;
    std::map<TrafficClass, uint32_t> m_classToInterface;
    uint32_t m_packetCount;
    
public:
    static TypeId GetTypeId() {
        static TypeId tid = TypeId("PolicyBasedRouter")
            .SetParent<Object>()
            .SetGroupName("Internet");
        return tid;
    }
    
    PolicyBasedRouter() : m_packetCount(0) {
        NS_LOG_FUNCTION(this);
        
        // Configuration de la classification par port
        m_portClassification[5004] = VIDEO_TRAFFIC;  // Port RTP
        m_portClassification[5005] = VIDEO_TRAFFIC;
        m_portClassification[21] = DATA_TRAFFIC;      // Port FTP
        m_portClassification[9] = DATA_TRAFFIC;       // Port Bulk
        
        // Mapping initial classe -> interface
        m_classToInterface[VIDEO_TRAFFIC] = 1;  // Interface primaire
        m_classToInterface[DATA_TRAFFIC] = 2;    // Interface secondaire
    }
    
    virtual ~PolicyBasedRouter() {
        NS_LOG_FUNCTION(this);
    }
    
    void SetRouterNode(Ptr<Node> node) {
        m_routerNode = node;
    }
    
    TrafficClass ClassifyTraffic(uint16_t srcPort, uint16_t dstPort, uint8_t dscp) {
        // Classification basée sur le port de destination
        if (m_portClassification.find(dstPort) != m_portClassification.end()) {
            return m_portClassification[dstPort];
        }
        
        // Classification basée sur le port source
        if (m_portClassification.find(srcPort) != m_portClassification.end()) {
            return m_portClassification[srcPort];
        }
        
        // Classification basée sur DSCP
        if (dscp == 46) { // EF (Expedited Forwarding)
            return VIDEO_TRAFFIC;
        } else if (dscp == 0) { // Best Effort
            return DATA_TRAFFIC;
        }
        
        return DEFAULT_TRAFFIC;
    }
    
    bool ProcessPacket(Ptr<NetDevice> device, Ptr<const Packet> packet,
                      uint16_t protocol, const Address& from,
                      const Address& to, NetDevice::PacketType packetType) {
        
        m_packetCount++;
        
        if (protocol != 0x0800) { // Non IPv4
            return true;
        }
        
        Ptr<Packet> pktCopy = packet->Copy();
        Ipv4Header ipHeader;
        pktCopy->RemoveHeader(ipHeader);
        
        uint16_t srcPort = 0, dstPort = 0;
        uint8_t dscp = ipHeader.GetTos() >> 2;
        
        // Extraire les ports selon le protocole
        if (ipHeader.GetProtocol() == UdpL4Protocol::PROT_NUMBER) {
            UdpHeader udpHeader;
            if (pktCopy->GetSize() >= 8) {
                pktCopy->RemoveHeader(udpHeader);
                srcPort = udpHeader.GetSourcePort();
                dstPort = udpHeader.GetDestinationPort();
            }
        } else if (ipHeader.GetProtocol() == TcpL4Protocol::PROT_NUMBER) {
            TcpHeader tcpHeader;
            if (pktCopy->GetSize() >= 20) {
                pktCopy->PeekHeader(tcpHeader);
                srcPort = tcpHeader.GetSourcePort();
                dstPort = tcpHeader.GetDestinationPort();
            }
        }
        
        // Classification
        TrafficClass tclass = ClassifyTraffic(srcPort, dstPort, dscp);
        
        if (m_packetCount % 100 == 0) {
            NS_LOG_INFO("Paquet classifié: " << 
                       (tclass == VIDEO_TRAFFIC ? "VIDEO" : 
                        tclass == DATA_TRAFFIC ? "DATA" : "DEFAULT") <<
                       " | Port: " << dstPort);
        }
        
        return true; // Continuer le traitement normal
    }
    
    void UpdateClassInterface(TrafficClass tclass, uint32_t interface) {
        m_classToInterface[tclass] = interface;
        NS_LOG_INFO("Interface mise à jour pour classe " << tclass << " -> " << interface);
    }
    
    uint32_t GetInterfaceForClass(TrafficClass tclass) {
        return m_classToInterface[tclass];
    }
};

// ========================================
// CLASSE: SdwanController
// ========================================

class SdwanController : public Object {
private:
    Ptr<Node> m_router;
    Ptr<PathMetricsMonitor> m_monitor;
    Ptr<PolicyBasedRouter> m_pbr;
    std::map<TrafficClass, PolicyRule> m_policies;
    EventId m_periodicEvent;
    Time m_evaluationInterval;
    uint32_t m_switchCount;
    
public:
    static TypeId GetTypeId() {
        static TypeId tid = TypeId("SdwanController")
            .SetParent<Object>()
            .SetGroupName("Applications");
        return tid;
    }
    
    SdwanController() : m_evaluationInterval(Seconds(1.0)), m_switchCount(0) {
        NS_LOG_FUNCTION(this);
    }
    
    virtual ~SdwanController() {
        NS_LOG_FUNCTION(this);
    }
    
    void SetRouter(Ptr<Node> router) {
        m_router = router;
    }
    
    void SetMetricsMonitor(Ptr<PathMetricsMonitor> monitor) {
        m_monitor = monitor;
    }
    
    void SetPbr(Ptr<PolicyBasedRouter> pbr) {
        m_pbr = pbr;
    }
    
    void AddPolicy(TrafficClass tclass, double latencyThresh, 
                  uint32_t primaryIf, uint32_t secondaryIf) {
        PolicyRule rule;
        rule.latencyThreshold = latencyThresh;
        rule.bandwidthThreshold = 5.0; // Mbps
        rule.primaryInterface = primaryIf;
        rule.secondaryInterface = secondaryIf;
        rule.currentInterface = primaryIf;
        m_policies[tclass] = rule;
        
        NS_LOG_INFO("Politique ajoutée pour classe " << tclass << 
                   " | Seuil latence: " << latencyThresh << " ms");
    }
    
    void Start() {
        NS_LOG_FUNCTION(this);
        m_periodicEvent = Simulator::Schedule(m_evaluationInterval, 
                                             &SdwanController::PeriodicPolicyEvaluation, this);
    }
    
    void Stop() {
        NS_LOG_FUNCTION(this);
        Simulator::Cancel(m_periodicEvent);
    }
    
    void PeriodicPolicyEvaluation() {
        NS_LOG_FUNCTION(this);
        
        for (auto& policyPair : m_policies) {
            TrafficClass tclass = policyPair.first;
            PolicyRule& rule = policyPair.second;
            
            // Récupérer les métriques
            double primaryLatency = m_monitor->GetInterfaceLatency(rule.primaryInterface);
            double secondaryLatency = m_monitor->GetInterfaceLatency(rule.secondaryInterface);
            
            double primaryBw = m_monitor->GetInterfaceBandwidth(rule.primaryInterface);
            double secondaryBw = m_monitor->GetInterfaceBandwidth(rule.secondaryInterface);
            
            bool shouldSwitch = false;
            uint32_t newInterface = rule.currentInterface;
            
            // Logique de décision pour le trafic vidéo
            if (tclass == VIDEO_TRAFFIC) {
                // Basculer vers le secondaire si le primaire est dégradé
                if (rule.currentInterface == rule.primaryInterface) {
                    if (primaryLatency > rule.latencyThreshold && 
                        secondaryLatency < primaryLatency * 0.8) {
                        shouldSwitch = true;
                        newInterface = rule.secondaryInterface;
                        std::cout << "[" << Simulator::Now().GetSeconds() << "s] ";
                        std::cout << "⚠️  BASCULEMENT: Flow_Video vers lien secondaire\n";
                        std::cout << "    Raison: Latence primaire (" << primaryLatency 
                                 << "ms) > seuil (" << rule.latencyThreshold << "ms)\n";
                    }
                }
                // Retour au primaire si la latence s'améliore
                else if (rule.currentInterface == rule.secondaryInterface) {
                    if (primaryLatency < rule.latencyThreshold * 0.7) {
                        shouldSwitch = true;
                        newInterface = rule.primaryInterface;
                        std::cout << "[" << Simulator::Now().GetSeconds() << "s] ";
                        std::cout << "✓ RETOUR: Flow_Video vers lien primaire\n";
                        std::cout << "    Raison: Latence primaire restaurée (" 
                                 << primaryLatency << "ms)\n";
                    }
                }
            }
            
            // Appliquer le basculement si nécessaire
            if (shouldSwitch) {
                rule.currentInterface = newInterface;
                m_pbr->UpdateClassInterface(tclass, newInterface);
                m_switchCount++;
            }
        }
        
        // Afficher les métriques périodiquement
        if (((int)Simulator::Now().GetSeconds()) % 5 == 0) {
            m_monitor->PrintMetrics();
        }
        
        // Replanifier
        m_periodicEvent = Simulator::Schedule(m_evaluationInterval,
                                             &SdwanController::PeriodicPolicyEvaluation, this);
    }
    
    uint32_t GetSwitchCount() {
        return m_switchCount;
    }
};

// ========================================
// FONCTION: Validation
// ========================================

void ValidatePbrOperation(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier) {
    std::cout << "\n========== VALIDATION PBR ==========\n";
    
    monitor->CheckForLostPackets();
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    
    double totalVideoLatency = 0.0;
    double totalDataLatency = 0.0;
    uint32_t videoFlows = 0;
    uint32_t dataFlows = 0;
    
    for (auto const& flow : stats) {
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);
        
        if (flow.second.rxPackets == 0) continue;
        
        double avgLatency = flow.second.delaySum.GetMilliSeconds() / flow.second.rxPackets;
        double throughput = (flow.second.rxBytes * 8.0) / 
                           (flow.second.timeLastRxPacket.GetSeconds() - 
                            flow.second.timeFirstTxPacket.GetSeconds()) / 1e6;
        double lossRate = (flow.second.lostPackets * 100.0) / flow.second.txPackets;
        
        std::cout << "\nFlux " << flow.first << ":\n";
        std::cout << "  Type: " << (tuple.destinationPort == 5004 ? "VIDEO (RTP)" : "DATA (Bulk)") << "\n";
        std::cout << "  " << tuple.sourceAddress << ":" << tuple.sourcePort << " -> "
                 << tuple.destinationAddress << ":" << tuple.destinationPort << "\n";
        std::cout << "  Paquets Tx/Rx: " << flow.second.txPackets << "/" << flow.second.rxPackets << "\n";
        std::cout << "  Latence moyenne: " << avgLatency << " ms\n";
        std::cout << "  Débit: " << throughput << " Mbps\n";
        std::cout << "  Taux de perte: " << lossRate << " %\n";
        
        if (tuple.destinationPort == 5004) {
            totalVideoLatency += avgLatency;
            videoFlows++;
        } else {
            totalDataLatency += avgLatency;
            dataFlows++;
        }
    }
    
    // Métriques globales
    std::cout << "\n--- MÉTRIQUES GLOBALES ---\n";
    if (videoFlows > 0) {
        std::cout << "Latence moyenne VIDEO: " << (totalVideoLatency / videoFlows) << " ms\n";
    }
    if (dataFlows > 0) {
        std::cout << "Latence moyenne DATA: " << (totalDataLatency / dataFlows) << " ms\n";
    }
    
    std::cout << "=====================================\n";
}

// ========================================
// FONCTION PRINCIPALE
// ========================================

int main(int argc, char *argv[]) {
    
    // Configuration des logs
    LogComponentEnable("PbrSimulation", LOG_LEVEL_INFO);
    
    // Paramètres de simulation
    double simulationTime = 30.0; // secondes
    uint32_t videoPacketSize = 160; // bytes
    uint32_t dataPacketSize = 1460; // bytes
    double videoInterval = 0.02; // 20ms (50 paquets/sec)
    
    std::cout << "\n╔════════════════════════════════════════════════════╗\n";
    std::cout << "║   SIMULATION NS-3: Policy-Based Routing (PBR)    ║\n";
    std::cout << "║          MediaStream Inc. - WAN Simulation         ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";
    
    // ========================================
    // CRÉATION DE LA TOPOLOGIE
    // ========================================
    
    NS_LOG_INFO("Création des nœuds...");
    NodeContainer nodes;
    nodes.Create(4); // Studio, Router, Cloud, Router2
    
    Ptr<Node> studioNode = nodes.Get(0);
    Ptr<Node> routerNode = nodes.Get(1);
    Ptr<Node> cloudNode = nodes.Get(2);
    Ptr<Node> router2Node = nodes.Get(3);
    
    // ========================================
    // CONFIGURATION DES LIENS
    // ========================================
    
    NS_LOG_INFO("Configuration des liens Point-to-Point...");
    
    PointToPointHelper p2pStudioRouter;
    p2pStudioRouter.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2pStudioRouter.SetChannelAttribute("Delay", StringValue("5ms"));
    
    // Lien primaire (faible latence, bande passante moyenne)
    PointToPointHelper p2pPrimary;
    p2pPrimary.SetDeviceAttribute("DataRate", StringValue("50Mbps"));
    p2pPrimary.SetChannelAttribute("Delay", StringValue("10ms"));
    
    // Lien secondaire (latence plus élevée, haute bande passante)
    PointToPointHelper p2pSecondary;
    p2pSecondary.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2pSecondary.SetChannelAttribute("Delay", StringValue("25ms"));
    
    // Installation des liens
    NetDeviceContainer devicesStudioRouter = p2pStudioRouter.Install(studioNode, routerNode);
    NetDeviceContainer devicesPrimary = p2pPrimary.Install(routerNode, cloudNode);
    NetDeviceContainer devicesSecondary = p2pSecondary.Install(router2Node, cloudNode);
    NetDeviceContainer devicesRouterRouter2 = p2pSecondary.Install(routerNode, router2Node);
    
    // ========================================
    // INSTALLATION DE LA PILE INTERNET
    // ========================================
    
    NS_LOG_INFO("Installation de la pile Internet...");
    InternetStackHelper stack;
    stack.Install(nodes);
    
    // Attribution des adresses IP
    Ipv4AddressHelper address;
    
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesStudioRouter = address.Assign(devicesStudioRouter);
    
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesPrimary = address.Assign(devicesPrimary);
    
    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesRouterRouter2 = address.Assign(devicesRouterRouter2);
    
    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesSecondary = address.Assign(devicesSecondary);
    
    // ========================================
    // CONFIGURATION DU ROUTAGE
    // ========================================
    
    NS_LOG_INFO("Configuration du routage global...");
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // ========================================
    // APPLICATION: FLOW_VIDEO (RTP-like)
    // ========================================
    
    NS_LOG_INFO("Configuration Flow_Video (RTP)...");
    uint16_t videoPort = 5004;
    
    UdpServerHelper videoServer(videoPort);
    ApplicationContainer videoServerApp = videoServer.Install(cloudNode);
    videoServerApp.Start(Seconds(1.0));
    videoServerApp.Stop(Seconds(simulationTime));
    
    UdpClientHelper videoClient(interfacesPrimary.GetAddress(1), videoPort);
    videoClient.SetAttribute("MaxPackets", UintegerValue(100000));
    videoClient.SetAttribute("Interval", TimeValue(MilliSeconds(20)));
    videoClient.SetAttribute("PacketSize", UintegerValue(videoPacketSize));
    
    ApplicationContainer videoClientApp = videoClient.Install(studioNode);
    videoClientApp.Start(Seconds(2.0));
    videoClientApp.Stop(Seconds(simulationTime));
    
    // ========================================
    // APPLICATION: FLOW_DATA (FTP-like)
    // ========================================
    
    NS_LOG_INFO("Configuration Flow_Data (Bulk Transfer)...");
    uint16_t dataPort = 9;
    
    PacketSinkHelper dataSink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), dataPort));
    ApplicationContainer dataSinkApp = dataSink.Install(cloudNode);
    dataSinkApp.Start(Seconds(1.0));
    dataSinkApp.Stop(Seconds(simulationTime));
    
    BulkSendHelper dataSource("ns3::TcpSocketFactory",
                              InetSocketAddress(interfacesPrimary.GetAddress(1), dataPort));
    dataSource.SetAttribute("MaxBytes", UintegerValue(50000000)); // 50 MB
    dataSource.SetAttribute("SendSize", UintegerValue(dataPacketSize));
    
    ApplicationContainer dataSourceApp = dataSource.Install(studioNode);
    dataSourceApp.Start(Seconds(2.5));
    dataSourceApp.Stop(Seconds(simulationTime));
    
    // ========================================
    // INSTALLATION DU FLOW MONITOR
    // ========================================
    
    NS_LOG_INFO("Installation du FlowMonitor...");
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    
    // ========================================
    // CRÉATION DES COMPOSANTS PBR
    // ========================================
    
    NS_LOG_INFO("Initialisation des composants PBR...");
    
    // PathMetricsMonitor
    Ptr<PathMetricsMonitor> metricsMonitor = CreateObject<PathMetricsMonitor>();
    metricsMonitor->Initialize(flowMonitor, classifier);
    metricsMonitor->EnableLatencyTracking(routerNode);
    Simulator::Schedule(Seconds(2.0), &PathMetricsMonitor::UpdateBandwidthMetrics, metricsMonitor);
    
    // PolicyBasedRouter
    Ptr<PolicyBasedRouter> pbr = CreateObject<PolicyBasedRouter>();
    pbr->SetRouterNode(routerNode);
    
    // Connecter le PBR aux callbacks
    routerNode->GetDevice(0)->SetPromiscReceiveCallback(
        MakeCallback(&PolicyBasedRouter::ProcessPacket, pbr)
    );
    
    // SdwanController
    Ptr<SdwanController> sdwanController = CreateObject<SdwanController>();
    sdwanController->SetRouter(routerNode);
    sdwanController->SetMetricsMonitor(metricsMonitor);
    sdwanController->SetPbr(pbr);
    
    // Ajouter la politique pour le trafic vidéo
    sdwanController->AddPolicy(VIDEO_TRAFFIC, 30.0, 1, 2);
    sdwanController->Start();
    
    // ========================================
    // SIMULATION D'UNE DÉGRADATION DU LIEN
    // ========================================
    
    // Introduire une dégradation à t=15s pour tester le basculement
    Simulator::Schedule(Seconds(15.0), [&]() {
        std::cout << "\n🔧 [15s] Simulation d'une dégradation du lien primaire...\n";
        // Note: Dans NS-3, modifier dynamiquement le délai nécessite une approche différente
        // Cette partie est conceptuelle et nécessiterait ErrorModel ou NetDevice modification
    });
    
    // ========================================
    // ACTIVATION DES TRACES PCAP
    // ========================================
    
    NS_LOG_INFO("Activation des traces PCAP...");
    p2pStudioRouter.EnableP