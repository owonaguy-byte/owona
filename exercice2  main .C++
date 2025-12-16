#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"
#include <fstream>
#include <map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("QoSMixedTraffic");

// ========== QUESTION 1: GÉNÉRATEURS DE TRAFIC AVEC MARQUAGE DSCP ==========

// Classe pour trafic VoIP
class VoipTrafficGenerator : public Application {
public:
    static TypeId GetTypeId(void);
    VoipTrafficGenerator();
    virtual ~VoipTrafficGenerator();
    
    void Setup(Ipv4Address destAddr, uint16_t destPort);
    
private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);
    void SendPacket(void);
    
    Ptr<Socket> m_socket;
    Ipv4Address m_destAddr;
    uint16_t m_destPort;
    uint32_t m_packetSize;      // 160 bytes (G.711 codec)
    Time m_interval;            // 20ms (50 pps)
    EventId m_sendEvent;
    uint32_t m_packetsSent;
    uint8_t m_dscp;             // DSCP marking
};

TypeId VoipTrafficGenerator::GetTypeId(void) {
    static TypeId tid = TypeId("VoipTrafficGenerator")
        .SetParent<Application>()
        .SetGroupName("Applications")
        .AddConstructor<VoipTrafficGenerator>();
    return tid;
}

VoipTrafficGenerator::VoipTrafficGenerator()
    : m_socket(0),
      m_packetSize(160),        // G.711: 64 kbps = 160 bytes @ 50pps
      m_interval(MilliSeconds(20)),
      m_packetsSent(0),
      m_dscp(46) {              // EF (Expedited Forwarding) DSCP 46
}

VoipTrafficGenerator::~VoipTrafficGenerator() {
    m_socket = 0;
}

void VoipTrafficGenerator::Setup(Ipv4Address destAddr, uint16_t destPort) {
    m_destAddr = destAddr;
    m_destPort = destPort;
}

void VoipTrafficGenerator::StartApplication(void) {
    if (!m_socket) {
        TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
        m_socket = Socket::CreateSocket(GetNode(), tid);
        
        InetSocketAddress remote = InetSocketAddress(m_destAddr, m_destPort);
        m_socket->Connect(remote);
        
        // Marquage DSCP EF (46) -> TOS = 0xB8 (46 << 2)
        m_socket->SetIpTos(0xB8);
        NS_LOG_INFO("VoIP: DSCP marqué à EF (46), TOS=0xB8");
    }
    
    SendPacket();
}

void VoipTrafficGenerator::StopApplication(void) {
    if (m_sendEvent.IsPending()) {
        Simulator::Cancel(m_sendEvent);
    }
    if (m_socket) {
        m_socket->Close();
    }
}

void VoipTrafficGenerator::SendPacket(void) {
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    m_socket->Send(packet);
    m_packetsSent++;
    
    if (m_packetsSent % 50 == 0) {
        NS_LOG_INFO("VoIP: " << m_packetsSent << " paquets envoyés à t=" 
                    << Simulator::Now().GetSeconds() << "s");
    }
    
    m_sendEvent = Simulator::Schedule(m_interval, 
                                      &VoipTrafficGenerator::SendPacket, this);
}

// Classe pour trafic FTP (bulk data)
class FtpTrafficGenerator : public Application {
public:
    static TypeId GetTypeId(void);
    FtpTrafficGenerator();
    virtual ~FtpTrafficGenerator();
    
    void Setup(Ipv4Address destAddr, uint16_t destPort);
    
private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);
    void SendBurst(void);
    void SendNextPacket(void);
    
    Ptr<Socket> m_socket;
    Ipv4Address m_destAddr;
    uint16_t m_destPort;
    uint32_t m_packetSize;      // 1500 bytes (MTU-sized)
    uint32_t m_burstPackets;    // Nombre de paquets par rafale
    uint32_t m_currentBurst;    // Compteur de rafale
    Time m_burstInterval;       // Intervalle entre rafales
    Time m_packetInterval;      // Intervalle entre paquets dans une rafale
    EventId m_sendEvent;
    uint32_t m_packetsSent;
    uint8_t m_dscp;
};

TypeId FtpTrafficGenerator::GetTypeId(void) {
    static TypeId tid = TypeId("FtpTrafficGenerator")
        .SetParent<Application>()
        .SetGroupName("Applications")
        .AddConstructor<FtpTrafficGenerator>();
    return tid;
}

FtpTrafficGenerator::FtpTrafficGenerator()
    : m_socket(0),
      m_packetSize(1500),
      m_burstPackets(100),
      m_currentBurst(0),
      m_burstInterval(Seconds(1.0)),
      m_packetInterval(MicroSeconds(100)),
      m_packetsSent(0),
      m_dscp(0) {               // Best Effort (DSCP 0)
}

FtpTrafficGenerator::~FtpTrafficGenerator() {
    m_socket = 0;
}

void FtpTrafficGenerator::Setup(Ipv4Address destAddr, uint16_t destPort) {
    m_destAddr = destAddr;
    m_destPort = destPort;
}

void FtpTrafficGenerator::StartApplication(void) {
    if (!m_socket) {
        TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
        m_socket = Socket::CreateSocket(GetNode(), tid);
        
        InetSocketAddress remote = InetSocketAddress(m_destAddr, m_destPort);
        m_socket->Connect(remote);
        
        // Marquage DSCP Best Effort (0) -> TOS = 0x00
        m_socket->SetIpTos(0x00);
        NS_LOG_INFO("FTP: DSCP marqué à BE (0), TOS=0x00");
    }
    
    SendBurst();
}

void FtpTrafficGenerator::StopApplication(void) {
    if (m_sendEvent.IsPending()) {
        Simulator::Cancel(m_sendEvent);
    }
    if (m_socket) {
        m_socket->Close();
    }
}

void FtpTrafficGenerator::SendBurst(void) {
    NS_LOG_INFO("FTP: Début rafale #" << (m_packetsSent / m_burstPackets + 1) 
                << " à t=" << Simulator::Now().GetSeconds() << "s");
    m_currentBurst = 0;
    SendNextPacket();
}

void FtpTrafficGenerator::SendNextPacket(void) {
    if (m_currentBurst < m_burstPackets) {
        Ptr<Packet> packet = Create<Packet>(m_packetSize);
        m_socket->Send(packet);
        m_packetsSent++;
        m_currentBurst++;
        
        m_sendEvent = Simulator::Schedule(m_packetInterval, 
                                          &FtpTrafficGenerator::SendNextPacket, this);
    } else {
        NS_LOG_INFO("FTP: Rafale terminée (" << m_burstPackets 
                    << " paquets, total: " << m_packetsSent << ")");
        m_sendEvent = Simulator::Schedule(m_burstInterval, 
                                          &FtpTrafficGenerator::SendBurst, this);
    }
}

// ========== QUESTION 3: COLLECTEUR DE MÉTRIQUES PERSONNALISÉ ==========

class QosMetricsCollector {
public:
    struct TrafficClassMetrics {
        std::string className;
        uint32_t txPackets;
        uint32_t rxPackets;
        uint32_t lostPackets;
        double totalDelay;
        double totalJitter;
        double previousDelay;
        uint64_t totalBytes;
        Time firstPacketTime;
        Time lastPacketTime;
        
        TrafficClassMetrics() : className(""), txPackets(0), rxPackets(0), 
            lostPackets(0), totalDelay(0.0), totalJitter(0.0), 
            previousDelay(0.0), totalBytes(0) {}
    };
    
    void RecordFlow(FlowId flowId, const FlowMonitor::FlowStats& stats, 
                    const Ipv4FlowClassifier::FiveTuple& tuple);
    void PrintReport();
    void ExportToCsv(const std::string& filename);
    
private:
    std::map<std::string, TrafficClassMetrics> m_classMetrics;
    
    std::string ClassifyFlow(const Ipv4FlowClassifier::FiveTuple& tuple);
    void CalculateJitter(TrafficClassMetrics& metrics, double currentDelay);
};

std::string QosMetricsCollector::ClassifyFlow(
    const Ipv4FlowClassifier::FiveTuple& tuple) {
    
    // Classification basée sur le port de destination
    if (tuple.destinationPort == 5060) {
        return "VoIP";
    } else if (tuple.destinationPort == 21 || tuple.destinationPort == 9) {
        return "FTP";
    }
    return "Other";
}

void QosMetricsCollector::RecordFlow(FlowId flowId, 
                                     const FlowMonitor::FlowStats& stats,
                                     const Ipv4FlowClassifier::FiveTuple& tuple) {
    std::string className = ClassifyFlow(tuple);
    
    if (m_classMetrics.find(className) == m_classMetrics.end()) {
        m_classMetrics[className] = TrafficClassMetrics();
        m_classMetrics[className].className = className;
    }
    
    TrafficClassMetrics& metrics = m_classMetrics[className];
    
    metrics.txPackets += stats.txPackets;
    metrics.rxPackets += stats.rxPackets;
    metrics.lostPackets += stats.lostPackets;
    metrics.totalBytes += stats.rxBytes;
    
    if (stats.rxPackets > 0) {
        double avgDelay = stats.delaySum.GetMilliSeconds() / stats.rxPackets;
        metrics.totalDelay += avgDelay * stats.rxPackets;
        
        // Calcul de la gigue (jitter)
        if (stats.rxPackets > 1) {
            // Jitter = variation moyenne du délai
            metrics.totalJitter += stats.jitterSum.GetMilliSeconds();
        }
    }
    
    if (stats.timeFirstTxPacket < metrics.firstPacketTime || 
        metrics.firstPacketTime.IsZero()) {
        metrics.firstPacketTime = stats.timeFirstTxPacket;
    }
    if (stats.timeLastRxPacket > metrics.lastPacketTime) {
        metrics.lastPacketTime = stats.timeLastRxPacket;
    }
}

void QosMetricsCollector::PrintReport() {
    NS_LOG_UNCOND("\n╔════════════════════════════════════════════════════════════════╗");
    NS_LOG_UNCOND("║          RAPPORT DE MÉTRIQUES QoS PAR CLASSE                  ║");
    NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════╝\n");
    
    for (auto const& entry : m_classMetrics) {
        const TrafficClassMetrics& m = entry.second;
        
        double lossRate = (m.txPackets > 0) ? 
            (100.0 * m.lostPackets / m.txPackets) : 0.0;
        double avgDelay = (m.rxPackets > 0) ? 
            (m.totalDelay / m.rxPackets) : 0.0;
        double avgJitter = (m.rxPackets > 1) ? 
            (m.totalJitter / (m.rxPackets - 1)) : 0.0;
        double duration = (m.lastPacketTime - m.firstPacketTime).GetSeconds();
        double throughput = (duration > 0) ? 
            (8.0 * m.totalBytes / duration / 1000000.0) : 0.0;
        
        NS_LOG_UNCOND("┌─ Classe de Trafic: " << m.className << " ─────────────────");
        NS_LOG_UNCOND("│");
        NS_LOG_UNCOND("│  📊 STATISTIQUES DE PAQUETS:");
        NS_LOG_UNCOND("│     • Paquets transmis    : " << m.txPackets);
        NS_LOG_UNCOND("│     • Paquets reçus       : " << m.rxPackets);
        NS_LOG_UNCOND("│     • Paquets perdus      : " << m.lostPackets);
        NS_LOG_UNCOND("│     • Taux de perte       : " << lossRate << " %");
        NS_LOG_UNCOND("│");
        NS_LOG_UNCOND("│  ⏱️  MÉTRIQUES DE LATENCE:");
        NS_LOG_UNCOND("│     • Délai moyen         : " << avgDelay << " ms");
        NS_LOG_UNCOND("│     • Gigue moyenne       : " << avgJitter << " ms");
        NS_LOG_UNCOND("│");
        NS_LOG_UNCOND("│  🚀 PERFORMANCE:");
        NS_LOG_UNCOND("│     • Débit               : " << throughput << " Mbps");
        NS_LOG_UNCOND("│     • Octets reçus        : " << m.totalBytes);
        NS_LOG_UNCOND("│     • Durée               : " << duration << " s");
        NS_LOG_UNCOND("└──────────────────────────────────────────────────────\n");
    }
    
    // Tableau comparatif
    NS_LOG_UNCOND("╔══════════════════════════════════════════════════════════════════╗");
    NS_LOG_UNCOND("║                    TABLEAU COMPARATIF                            ║");
    NS_LOG_UNCOND("╠════════════╦══════════╦══════════╦═══════════╦═══════════════════╣");
    NS_LOG_UNCOND("║   Classe   ║  Délai   ║  Gigue   ║ Perte (%) ║   Débit (Mbps)   ║");
    NS_LOG_UNCOND("╠════════════╬══════════╬══════════╬═══════════╬═══════════════════╣");
    
    for (auto const& entry : m_classMetrics) {
        const TrafficClassMetrics& m = entry.second;
        double lossRate = (m.txPackets > 0) ? (100.0 * m.lostPackets / m.txPackets) : 0.0;
        double avgDelay = (m.rxPackets > 0) ? (m.totalDelay / m.rxPackets) : 0.0;
        double avgJitter = (m.rxPackets > 1) ? (m.totalJitter / (m.rxPackets - 1)) : 0.0;
        double duration = (m.lastPacketTime - m.firstPacketTime).GetSeconds();
        double throughput = (duration > 0) ? (8.0 * m.totalBytes / duration / 1000000.0) : 0.0;
        
        char buffer[256];
        snprintf(buffer, sizeof(buffer), 
                 "║ %-10s ║ %6.2f ms ║ %6.2f ms ║  %6.2f   ║      %6.2f       ║",
                 m.className.c_str(), avgDelay, avgJitter, lossRate, throughput);
        NS_LOG_UNCOND(buffer);
    }
    
    NS_LOG_UNCOND("╚════════════╩══════════╩══════════╩═══════════╩═══════════════════╝\n");
}

void QosMetricsCollector::ExportToCsv(const std::string& filename) {
    std::ofstream file(filename);
    file << "Classe,Paquets_TX,Paquets_RX,Paquets_Perdus,Taux_Perte_%,";
    file << "Delai_Moyen_ms,Gigue_Moyenne_ms,Debit_Mbps\n";
    
    for (auto const& entry : m_classMetrics) {
        const TrafficClassMetrics& m = entry.second;
        double lossRate = (m.txPackets > 0) ? (100.0 * m.lostPackets / m.txPackets) : 0.0;
        double avgDelay = (m.rxPackets > 0) ? (m.totalDelay / m.rxPackets) : 0.0;
        double avgJitter = (m.rxPackets > 1) ? (m.totalJitter / (m.rxPackets - 1)) : 0.0;
        double duration = (m.lastPacketTime - m.firstPacketTime).GetSeconds();
        double throughput = (duration > 0) ? (8.0 * m.totalBytes / duration / 1000000.0) : 0.0;
        
        file << m.className << "," << m.txPackets << "," << m.rxPackets << ","
             << m.lostPackets << "," << lossRate << "," << avgDelay << ","
             << avgJitter << "," << throughput << "\n";
    }
    
    file.close();
    NS_LOG_UNCOND("Métriques exportées vers: " << filename);
}

// ========== PROGRAMME PRINCIPAL ==========

int main(int argc, char *argv[]) {
    
    bool enableQos = true;
    bool enableCongestion = true;
    uint32_t voipClients = 5;
    uint32_t ftpClients = 3;
    
    CommandLine cmd;
    cmd.AddValue("enableQos", "Activer QoS (priority queuing)", enableQos);
    cmd.AddValue("enableCongestion", "Créer une congestion du lien", enableCongestion);
    cmd.AddValue("voipClients", "Nombre de clients VoIP", voipClients);
    cmd.AddValue("ftpClients", "Nombre de clients FTP", ftpClients);
    cmd.Parse(argc, argv);
    
    Time::SetResolution(Time::NS);
    LogComponentEnable("QoSMixedTraffic", LOG_LEVEL_INFO);
    
    NS_LOG_UNCOND("\n╔════════════════════════════════════════════════════════════════╗");
    NS_LOG_UNCOND("║     SIMULATION QoS - TRAFIC MIXTE VoIP & FTP                  ║");
    NS_LOG_UNCOND("╠════════════════════════════════════════════════════════════════╣");
    NS_LOG_UNCOND("║  QoS activée         : " << (enableQos ? "OUI" : "NON"));
    NS_LOG_UNCOND("║  Congestion          : " << (enableCongestion ? "OUI" : "NON"));
    NS_LOG_UNCOND("║  Clients VoIP        : " << voipClients);
    NS_LOG_UNCOND("║  Clients FTP         : " << ftpClients);
    NS_LOG_UNCOND("╚════════════════════════════════════════════════════════════════╝\n");
    
    // ========== CRÉATION DE LA TOPOLOGIE ==========
    
    NodeContainer nodes;
    nodes.Create(3); // 0=Client, 1=Router, 2=Server
    
    // Configuration du lien avec bande passante limitée
    PointToPointHelper p2pClientRouter;
    p2pClientRouter.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2pClientRouter.SetChannelAttribute("Delay", StringValue("2ms"));
    
    PointToPointHelper p2pRouterServer;
    // QUESTION 4: Lien WAN avec bande passante limitée pour créer congestion
    p2pRouterServer.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2pRouterServer.SetChannelAttribute("Delay", StringValue("20ms"));
    
    NetDeviceContainer devicesClientRouter = p2pClientRouter.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer devicesRouterServer = p2pRouterServer.Install(nodes.Get(1), nodes.Get(2));
    
    // ========== QUESTION 2: CONFIGURATION QoS ==========
    
    TrafficControlHelper tch;
    
    if (enableQos) {
        NS_LOG_INFO("Configuration de la discipline de file d'attente prioritaire...");
        
        // Utilisation de PfifoFastQueueDisc (3 bandes de priorité)
        tch.SetRootQueueDisc("ns3::PfifoFastQueueDisc",
                             "MaxSize", StringValue("1000p"));
        
        // Installation sur l'interface du routeur vers le serveur
        QueueDiscContainer qdiscs = tch.Install(devicesRouterServer.Get(0));
        
        NS_LOG_INFO("✓ QoS configurée: PfifoFastQueueDisc avec 3 niveaux de priorité");
        NS_LOG_INFO("  • Bande 0 (haute priorité): TOS bits [0-3] - VoIP (DSCP EF)");
        NS_LOG_INFO("  • Bande 1 (priorité moyenne): TOS bits [4-7]");
        NS_LOG_INFO("  • Bande 2 (basse priorité): TOS bits [8-15] - FTP (DSCP BE)");
    }
    
    // Installation de la pile Internet
    InternetStackHelper stack;
    stack.Install(nodes);
    
    // Attribution des adresses IP
    Ipv4AddressHelper address;
    
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifacesClientRouter = address.Assign(devicesClientRouter);
    
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifacesRouterServer = address.Assign(devicesRouterServer);
    
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // ========== CONFIGURATION DES APPLICATIONS ==========
    
    // Serveur VoIP sur le serveur (port 5060)
    uint16_t voipPort = 5060;
    PacketSinkHelper voipSink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), voipPort));
    ApplicationContainer voipServerApp = voipSink.Install(nodes.Get(2));
    voipServerApp.Start(Seconds(1.0));
    voipServerApp.Stop(Seconds(30.0));
    
    // Serveur FTP sur le serveur (port 21)
    uint16_t ftpPort = 21;
    PacketSinkHelper ftpSink("ns3::UdpSocketFactory",
                             InetSocketAddress(Ipv4Address::GetAny(), ftpPort));
    ApplicationContainer ftpServerApp = ftpSink.Install(nodes.Get(2));
    ftpServerApp.Start(Seconds(1.0));
    ftpServerApp.Stop(Seconds(30.0));
    
    // Création de plusieurs clients VoIP
    for (uint32_t i = 0; i < voipClients; i++) {
        Ptr<VoipTrafficGenerator> voipApp = CreateObject<VoipTrafficGenerator>();
        voipApp->Setup(ifacesRouterServer.GetAddress(1), voipPort);
        nodes.Get(0)->AddApplication(voipApp);
        voipApp->SetStartTime(Seconds(2.0 + i * 0.1));
        voipApp->SetStopTime(Seconds(30.0));
    }
    
    // Création de plusieurs clients FTP
    for (uint32_t i = 0; i < ftpClients; i++) {
        Ptr<FtpTrafficGenerator> ftpApp = CreateObject<FtpTrafficGenerator>();
        ftpApp->Setup(ifacesRouterServer.GetAddress(1), ftpPort);
        nodes.Get(0)->AddApplication(ftpApp);
        
        if (enableCongestion) {
            // Démarrage échelonné pour créer congestion progressive
            ftpApp->SetStartTime(Seconds(5.0 + i * 2.0));
        } else {
            ftpApp->SetStartTime(Seconds(2.5 + i * 0.1));
        }
        ftpApp->SetStopTime(Seconds(30.0));
    }
    
    // ========== MONITORING ==========
    
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    
    // ========== EXÉCUTION ==========
    
    NS_LOG_UNCOND("\n🚀 Démarrage de la simulation...\n");
    
    Simulator::Stop(Seconds(30.0));
    Simulator::Run();
    
    // ========== ANALYSE DES RÉSULTATS ==========
    
    NS_LOG_UNCOND("\n📈 Analyse des résultats...\n");
    
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    
    QosMetricsCollector collector;
    
    for (auto const& stat : stats) {
        Ipv4FlowClassifier::FiveTuple t = classifier->