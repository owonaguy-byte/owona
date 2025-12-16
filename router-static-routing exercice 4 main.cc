/**
 * Two nodes separated by a router with static routing
 *
 * Network Topology:
 *
 *   Network 1 (10.1.1.0/24)          Network 2 (10.1.2.0/24)
 *
 *   n0 -------------------- n1 (Router) -------------------- n2
 *      point-to-point                    point-to-point
 *      5Mbps, 2ms                        5Mbps, 2ms
 *
  # n0 is on network 10.1.1.0/24 (IP: 10.1.1.1)
 * - n1 is the router with two interfaces:
 *     - Interface 1: 10.1.1.2 (connected to n0)
 *     - Interface 2: 10.1.2.1 (connected to n2)
 * - n2 is on network 10.1.2.0/24 (IP: 10.1.2.2)
 * - Static routes configured on n0 and n2 to reach each other through n1
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TwoNodesWithRouter");

int
main(int argc, char* argv[])
{
    // Enable logging
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    // Create three nodes: n0 (client), n1 (router), n2 (server)
    NodeContainer nodes;
    nodes.Create(3);

    Ptr<Node> n0 = nodes.Get(0); // Client
    Ptr<Node> n1 = nodes.Get(1); // Router
    Ptr<Node> n2 = nodes.Get(2); // Server

    // Create point-to-point links
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    // Link 1: n0 <-> n1 (Network 1)
    NodeContainer link1Nodes(n0, n1);
    NetDeviceContainer link1Devices = p2p.Install(link1Nodes);

    // Link 2: n1 <-> n2 (Network 2)
    NodeContainer link2Nodes(n1, n2);
    NetDeviceContainer link2Devices = p2p.Install(link2Nodes);

    // Install mobility model to keep nodes at fixed positions
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // Set the positions for each node
    Ptr<MobilityModel> mob0 = n0->GetObject<MobilityModel>();
    Ptr<MobilityModel> mob1 = n1->GetObject<MobilityModel>();
    Ptr<MobilityModel> mob2 = n2->GetObject<MobilityModel>();

    // Triangle layout: Router at top, Client and Server at bottom corners
    mob0->SetPosition(Vector(5.0, 15.0, 0.0));  // Client bottom-left
    mob1->SetPosition(Vector(10.0, 2.0, 0.0));  // Router top-center
    mob2->SetPosition(Vector(15.0, 15.0, 0.0)); // Server bottom-right

    // Install Internet stack on all nodes
    InternetStackHelper stack;
    stack.Install(nodes);

    // Assign IP addresses to Network 1 (10.1.1.0/24)
    Ipv4AddressHelper address1;
    address1.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces1 = address1.Assign(link1Devices);
    // interfaces1.GetAddress(0) = 10.1.1.1 (n0)
    // interfaces1.GetAddress(1) = 10.1.1.2 (n1's first interface)

    // Assign IP addresses to Network 2 (10.1.2.0/24)
    Ipv4AddressHelper address2;
    address2.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces2 = address2.Assign(link2Devices);
    // interfaces2.GetAddress(0) = 10.1.2.1 (n1's second interface)
    // interfaces2.GetAddress(1) = 10.1.2.2 (n2)

    // *** Configure Static Routing ***

    // Enable IP forwarding on the router (n1)
    Ptr<Ipv4> ipv4Router = n1->GetObject<Ipv4>();
    ipv4Router->SetAttribute("IpForward", BooleanValue(true));

    // Get static routing protocol helper
    Ipv4StaticRoutingHelper staticRoutingHelper;

    // Configure routing on n0 (client)
    // n0 needs to know that to reach 10.1.2.0/24, it should go through 10.1.1.2 (router's
    // interface)
    Ptr<Ipv4StaticRouting> staticRoutingN0 =
        staticRoutingHelper.GetStaticRouting(n0->GetObject<Ipv4>());
    staticRoutingN0->AddNetworkRouteTo(
        Ipv4Address("10.1.2.0"),   // Destination network
        Ipv4Mask("255.255.255.0"), // Network mask
        Ipv4Address("10.1.1.2"),   // Next hop (router's interface on network 1)
        1                          // Interface index
    );

    // Configure routing on n2 (server)
    // n2 needs to know that to reach 10.1.1.0/24, it should go through 10.1.2.1 (router's
    // interface)
    Ptr<Ipv4StaticRouting> staticRoutingN2 =
        staticRoutingHelper.GetStaticRouting(n2->GetObject<Ipv4>());
    staticRoutingN2->AddNetworkRouteTo(
        Ipv4Address("10.1.1.0"),   // Destination network
        Ipv4Mask("255.255.255.0"), // Network mask
        Ipv4Address("10.1.2.1"),   // Next hop (router's interface on network 2)
        1                          // Interface index
    );

    // Note: Router (n1) doesn't need explicit routes as it's directly connected to both networks

    // Print routing tables for verification
    Ptr<OutputStreamWrapper> routingStream =
        Create<OutputStreamWrapper>("scratch/router-static-routing.routes", std::ios::out);
    staticRoutingHelper.PrintRoutingTableAllAt(Seconds(1.0), routingStream);

    std::cout << "\n=== Network Configuration ===\n";
    std::cout << "Node 0 (Client): " << interfaces1.GetAddress(0) << " (Network 1)\n";
    std::cout << "Node 1 (Router) Interface 1: " << interfaces1.GetAddress(1) << " (Network 1)\n";
    std::cout << "Node 1 (Router) Interface 2: " << interfaces2.GetAddress(0) << " (Network 2)\n";
    std::cout << "Node 2 (Server): " << interfaces2.GetAddress(1) << " (Network 2)\n";
    std::cout << "=============================\n\n";

    // Create UDP Echo Server on n2 (10.1.2.2)
    uint16_t port = 9;
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(n2);
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));

    // Create UDP Echo Client on n0 targeting n2's IP address
    UdpEchoClientHelper echoClient(interfaces2.GetAddress(1), port); // Server's IP on Network 2
    echoClient.SetAttribute("MaxPackets", UintegerValue(3));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(n0);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(10.0));

    // *** NetAnim Configuration ***
    AnimationInterface anim("scratch/router-static-routing.xml");

    // Node positions are already set via MobilityModel above
    // NetAnim will automatically use the mobility model positions

    // Set node descriptions
    anim.UpdateNodeDescription(n0, "Client\n10.1.1.1");
    anim.UpdateNodeDescription(n1, "Router\n10.1.1.2 | 10.1.2.1");
    anim.UpdateNodeDescription(n2, "Server\n10.1.2.2");

    // Set node colors
    anim.UpdateNodeColor(n0, 0, 255, 0);   // Green for client
    anim.UpdateNodeColor(n1, 255, 255, 0); // Yellow for router
    anim.UpdateNodeColor(n2, 0, 0, 255);   // Blue for server

    // Enable PCAP tracing on all devices for Wireshark analysis
    p2p.EnablePcapAll("scratch/router-static-routing");

    // Run simulation
    Simulator::Stop(Seconds(11.0));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "\n=== Simulation Complete ===\n";
    std::cout << "Animation trace saved to: scratch/router-static-routing.xml\n";
    std::cout << "Routing tables saved to: scratch/router-static-routing.routes\n";
    std::cout << "PCAP traces saved to: scratch/router-static-routing-*.pcap\n";
    std::cout << "Open the XML file with NetAnim to visualize the simulation.\n";

    return 0;
}
