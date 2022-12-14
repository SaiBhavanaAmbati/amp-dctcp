/*
 * MultiPath-TCP (MPTCP) implementation.
 * Programmed by Morteza Kheirkhah from University of Sussex.
 * Some codes here are modeled from ns3::TCPNewReno implementation.
 * Email: m.kheirkhah@sussex.ac.uk
 */
#define NS_LOG_APPEND_CONTEXT \
  if (m_node) { std::clog << Simulator::Now ().GetSeconds () << " [node " << m_node->GetId () << "] "; }

#include <algorithm>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <cassert>
#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/mp-tcp-socket-base.h"
#include "ns3/tcp-l4-protocol.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/error-model.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/pointer.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/object-vector.h"
#include "ns3/flow-id-tag.h"
#include "ns3/ce-tag.h"
#include "ns3/ece-tag.h"
#include "ns3/ect-tag.h"
#include "ns3/control-tag.h"
#include "ns3/ecmp-tag.h"

#define PLOT
#define PLOT_DCTCP
//#define MANUAL_DROP

#define RAND_GAP

NS_LOG_COMPONENT_DEFINE("MpTcpSocketBase");
using namespace std;
namespace ns3
{
NS_OBJECT_ENSURE_REGISTERED(MpTcpSocketBase);

TypeId
MpTcpSocketBase::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::MpTcpSocketBase")
      .SetParent<TcpSocketBase> ()
      .AddConstructor<MpTcpSocketBase> ()
      .AddAttribute ("CongestionControl",
                     "Congestion control algorithm",
                     EnumValue (Linked_Increases),
                     MakeEnumAccessor (&MpTcpSocketBase::SetCongestionCtrlAlgo),
                     MakeEnumChecker (Uncoupled_TCPs, "Uncoupled_TCPs", Fully_Coupled, "Fully_Coupled", RTT_Compensator, "RTT_Compensator",
                           Linked_Increases, "Linked_Increases", COUPLED_INC, "COUPLED_INC", COUPLED_EPSILON, "COUPLED_EPSILON",
                           COUPLED_SCALABLE_TCP, "COUPLED_SCALABLE_TCP", COUPLED_FULLY, "COUPLED_FULLY", UNCOUPLED, "UNCOUPLED",  Fast_Uncoupled, "Fast_Uncoupled", Fast_Increases, "Fast_Increases", XCA, "XCA"))
      .AddAttribute ("SchedulingAlgorithm",
                     "Algorithm for data distribution between sub-flows",
                     EnumValue (Round_Robin),
                     MakeEnumAccessor (&MpTcpSocketBase::SetDataDistribAlgo),
                     MakeEnumChecker (Round_Robin, "Round_Robin"))
      .AddAttribute ("PathManagement",
                     "Mechanism for establishing new sub-flows",
                     EnumValue (NdiffPorts),
                     MakeEnumAccessor (&MpTcpSocketBase::SetPathManager),
                     MakeEnumChecker (Default, "Default", FullMesh, "FullMesh", NdiffPorts, "NdiffPorts"))
      .AddAttribute ("MaxSubflows",
                     "Maximum number of sub-flows per each mptcp connection",
                     UintegerValue (8),
                     MakeUintegerAccessor (&MpTcpSocketBase::maxSubflows),
                     MakeUintegerChecker<uint8_t> ())
      .AddAttribute ("RandomGap",
                     "Random gap between subflows setup",
                     UintegerValue (50),
                     MakeUintegerAccessor (&MpTcpSocketBase::m_rGap),
                     MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("Subflows",
                     "The list of sub-flows associated to this protocol.",
                     ObjectVectorValue (),
                     MakeObjectVectorAccessor (&MpTcpSocketBase::subflows),
                     MakeObjectVectorChecker<MpTcpSocketBase> ())
      .AddAttribute ("ShortFlowTCP", "Use TCP for short flows",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_shortFlowTCP),
                     MakeBooleanChecker ())
      .AddAttribute ("BackgroundFlow", "Large flows are background flows",
                     BooleanValue (true),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_backgroundFlow),
                     MakeBooleanChecker ())
      .AddAttribute ("AlphaPerAck", " Update alpha per ACK ",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_alphaPerAck),
                     MakeBooleanChecker ())
      .AddAttribute ("DctcpAlphaPerAck", " Update DCTCP alpha per ACK ",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_dctcpAlphaPerAck),
                     MakeBooleanChecker ())
      .AddAttribute ("DctcpFastReTxRecord", " Recording Fraction/Alpha at FastReTx point ",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_dctcpFastReTxRecord),
                     MakeBooleanChecker ())
      .AddAttribute ("ShortPlotting", " Activate large flow plotting ",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_shortPlotting),
                     MakeBooleanChecker ())
      .AddAttribute ("LargePlotting", " Activate short flow plotting ",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_largePlotting),
                     MakeBooleanChecker ())
      .AddAttribute ("RatePlotSf", " Activate rate plotting @ subflow level",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_ratePlotSf),
                     MakeBooleanChecker ())
      .AddAttribute ("RatePlotCl", " Activate rate plotting @ connection level",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_ratePlotCl),
                     MakeBooleanChecker ())
      .AddAttribute ("RateInterval",
                     "Sample rate interval for rate plotting, default is 0.1 seconds",
                     DoubleValue (0.1),
                     MakeDoubleAccessor (&MpTcpSocketBase::m_rateInterval),
                     MakeDoubleChecker<double>())
      .AddAttribute ("DCTCP",
                     "DCTCP flavored socket",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_isDCTCPEnabled),
                     MakeBooleanChecker ())
      .AddAttribute ("DCTCPWeight",
                     "Weight for calculating DCTCP's alpha parameter",
                     DoubleValue (1.0 / 16.0), MakeDoubleAccessor (&MpTcpSocketBase::m_g),
                     MakeDoubleChecker<double> (0, 1))
      .AddAttribute ("InstantRate",
                     "The instant rate of communication",
                     DoubleValue (0.0),
                     MakeDoubleAccessor (&MpTcpSocketBase::m_instRate),
                     MakeDoubleChecker<double>() )
      .AddAttribute ("beta",
                     "The backoff factor of cwnd when receiving ce ",
                     UintegerValue (4),
                     MakeUintegerAccessor (&MpTcpSocketBase::m_backoffBeta),
                     MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("gamma",
                     "The number of packets backlogging in network",
                     UintegerValue (1),
                     MakeUintegerAccessor (&MpTcpSocketBase::m_initGamma),
                     MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("RwndScale",
                     "Receive window scale",
                     UintegerValue (1),
                     MakeUintegerAccessor (&MpTcpSocketBase::m_rwndScale),
                     MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("ECN",
                     "ECN flavored socket",
                     BooleanValue (false),
                     MakeBooleanAccessor (&MpTcpSocketBase::m_ecn),
                     MakeBooleanChecker ())
     .AddAttribute ("DisjoinedPath",
                    "Long Flows use disjointed path at the aggregation layer only",
                    BooleanValue (false),
                    MakeBooleanAccessor (&MpTcpSocketBase::m_disjoinPath),
                    MakeBooleanChecker ())
    .AddAttribute ("IsAdaptiveSubflow",
                   "Dynamic subflow adjustment, especially in the incast scenarios",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MpTcpSocketBase::m_isAdaptiveSubflow),
                   MakeBooleanChecker ())
   .AddAttribute ("IncastThresh",
                  "Incast Threshold",
                  UintegerValue (10),
                  MakeUintegerAccessor (&MpTcpSocketBase::m_CongestionThreshold),
                  MakeUintegerChecker<uint32_t> ())
   .AddAttribute ("IncastExitThresh",
                  "Incast Exit Threshold",
                  UintegerValue (8),
                  MakeUintegerAccessor (&MpTcpSocketBase::m_CongestionExitThreshold),
                  MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("CwndMin",
                   "Minimum cwnd size",
                   UintegerValue (1),
                   MakeUintegerAccessor (&MpTcpSocketBase::m_cwndMin),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("ADCT",
                   "Enabling Adoptive Data Center TCP (ADCT)",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MpTcpSocketBase::m_ADCT),
                   MakeBooleanChecker ())
    .AddAttribute ("ADCTg",
                   "Adoptive Weight for calculating DCTCP's alpha parameter after a threshold reaches",
                   DoubleValue (0.6),
                   MakeDoubleAccessor (&MpTcpSocketBase::m_ADCTg),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("ADCTthresh",
                   "ADCT switching threshold",
                   UintegerValue (10000000), // 10MB
                   MakeUintegerAccessor (&MpTcpSocketBase::m_ADCTthresh),
                   MakeUintegerChecker<uint32_t> ());
  return tid;
}

MpTcpSocketBase::MpTcpSocketBase () :
    subflows (0), localAddrs (0), remoteAddrs (0)
//:
//    m_node(node), m_tcp(node->GetObject<TcpL4Protocol>()), mpState(MP_NONE), mpSendState(MP_NONE), mpRecvState(MP_NONE), mpEnabled(false), addrAdvertised(
//        false), mpTokenRegister(false), subflows(0), localAddrs(0), remoteAddrs(0), lastUsedsFlowIdx(0), totalCwnd(0), localToken(0), remoteToken(0), client(
//        false), server(false), remoteRecvWnd(1), segmentSize(0), nextTxSequence(1), nextRxSequence(1)
{
  NS_LOG_FUNCTION(this);
  //m_node = node;
  //m_tcp = node->GetObject<TcpL4Protocol>();
  mpSendState = MP_NONE;
  mpRecvState = MP_NONE;
  mpEnabled = false;
  addrAdvertised = false;
  mpTokenRegister = false;
  lastUsedsFlowIdx = 0;
  totalCwnd = 0;
  localToken = 0;
  remoteToken = 0;
  client = false;
  server = false;
  remoteRecvWnd = 1;
  segmentSize = 0;
  nextTxSequence = 1;
  nextRxSequence = 1;
  //lastNextTxSequence = nextTxSequence;
  //gnu.SetOutFile("allPlots.pdf");
  mod = 60;
  // --------------
  fLowStartTime = 0;
  FullAcks = 0;
  pAck = 0;
  TimeOuts = 0;
  FastReTxs = 0;
  FastRecoveries = 0;
  SlowDownHits = 0;
  flowCompletionTime = true;
  //TxBytes = 0;
  flowType = "NULL";
  flowLayer = "NULL";
  socketModel = "NULL";
  outputFileName = "NULL";
  outputFileNameDctcp = "NULL";
  hasSampleListDone = false;
  flowSize = 0;
  m_totalSentBytes = 0;
  alpha = 1; // alpha is 1 by default
  _e = 1;    // epsilon 1 by default
  a = A_SCALE;
  m_slowDownEcnLike = false;
  m_dctcpFastAlpha  = false;
  m_initialRand = rand() % 230;
  m_CongestionRound = 0;
  m_CongestionReDoCounter = 0;
  m_CongestionEnterHits = 0;
  m_CongestionExitHits = 0;
  m_ADCTcontrol = true;
  m_capacity = "100Mbps";
  Callback<void, Ptr<Socket> > vPS = MakeNullCallback<void, Ptr<Socket> > ();
  Callback<void, Ptr<Socket>, const Address &> vPSA = MakeNullCallback<void, Ptr<Socket>, const Address &> ();
  Callback<void, Ptr<Socket>, uint32_t> vPSUI = MakeNullCallback<void, Ptr<Socket>, uint32_t> ();
  SetConnectCallback (vPS, vPS);
  SetDataSentCallback (vPSUI);
  SetSendCallback (vPSUI);
  SetRecvCallback (vPS);
}

MpTcpSocketBase::~MpTcpSocketBase (void)
{
  NS_LOG_FUNCTION(this);
  m_node = 0;
  /*
   * Upon Bind, an Ipv4Endpoint is allocated and set to m_endPoint, and
   * DestroyCallback is set to TcpSocketBase::Destroy. If we called
   * m_tcp->DeAllocate, it will destroy its Ipv4EndpointDemux::DeAllocate,
   * which in turn destroys my m_endPoint, and in turn invokes
   * TcpSocketBase::Destroy to nullify m_node, m_endPoint, and m_tcp.
   */
  if (m_endPoint != 0)
    {
      NS_ASSERT(m_tcp != 0);
      m_tcp->DeAllocate (m_endPoint);
      NS_ASSERT(m_endPoint == 0);
    }
  m_tcp = 0;
  CancelAllSubflowTimers ();
  NS_LOG_INFO(Simulator::Now().GetSeconds() << " ["<< this << "] ~MpTcpSocketBase -> m_node: " << m_node << " m_tcp: " << m_tcp << " m_endPoint: " << m_endPoint);
}

/** Configure the endpoint to a local address. Called by Connect() if Bind() didn't specify one. */
int
MpTcpSocketBase::SetupEndpoint ()
{
  NS_LOG_FUNCTION (this);
  Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4> ();
  NS_ASSERT(ipv4 != 0);
  if (ipv4->GetRoutingProtocol () == 0)
    {
      NS_FATAL_ERROR("No Ipv4RoutingProtocol in the node");
    }

  // Temporary solution to solve ECMP related issue when RouteOutput() is called.
  TcpHeader tcpHeader;
  Ptr<Packet> pkt = Create<Packet> ();
  pkt->AddHeader (tcpHeader);

  // Create a dummy packet, then ask the routing function for the best output interface's address
  Ipv4Header header;
  header.SetProtocol (6);
  header.SetDestination (m_endPoint->GetPeerAddress ());
  Socket::SocketErrno errno_;
  Ptr<Ipv4Route> route;
  Ptr<NetDevice> oif = m_boundnetdevice; // m_boundnetdevice is allocated to 0 by the default constructor of ns3::Socket.
  route = ipv4->GetRoutingProtocol ()->RouteOutput (pkt, header, oif, errno_);
  //route = ipv4->GetRoutingProtocol()->RouteOutput(Ptr<Packet>(), header, oif, errno_);
  if (route == 0)
    {
      NS_LOG_LOGIC ("Route to " << m_endPoint->GetPeerAddress () << " does not exist");NS_LOG_ERROR (errno_);
      m_errno = errno_;
      return -1;
    }NS_LOG_LOGIC ("Route exists");

  // Setup local address for this endpoint
  m_endPoint->SetLocalAddress (route->GetSource ());
  return 0;
}

void
MpTcpSocketBase::EstimateRtt (const TcpHeader& TcpHeader)
{
  NS_LOG_FUNCTION_NOARGS();
}

/** Called by ForwardUp() to estimate RTT */
void
MpTcpSocketBase::EstimateRtt (uint8_t sFlowIdx, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION(this << (int)sFlowIdx);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  bool isECNEcho = (mptcpHeader.GetFlags () == (TcpHeader::ACK) && (m_eceBit > 0));
  Time nextRtt = sFlow->rtt->AckSeq (mptcpHeader.GetAckNumber (), isECNEcho);

  sFlow->lastMeasuredRtt = nextRtt;
  if (sFlow->lastMeasuredRtt != Seconds (0.0))
    {
      sFlow->m_baseRTT = Min (sFlow->m_baseRTT, sFlow->lastMeasuredRtt);
      sFlow->m_minRTT = Min (sFlow->m_minRTT, sFlow->lastMeasuredRtt);
    }

  // Plotting
#ifdef PLOT
  sFlow->_RTT.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->lastMeasuredRtt.GetMilliSeconds ()));
  sFlow->_AvgRTT.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->rtt->GetCurrentEstimate ().GetMilliSeconds ()));
  sFlow->_RTO.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->rtt->RetransmitTimeout ().GetMilliSeconds ()));
#endif

#ifdef PLOT_DCTCP
  sFlow->DCTCP_ALPHA_RTT.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->rtt->m_alpha));
  sFlow->DCTCP_FRACTION_RTT.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->rtt->m_fracMarkPkt));
#endif
}

/* Read options from incoming packets */
bool
MpTcpSocketBase::ReadOptions (Ptr<Packet> pkt, const TcpHeader& mptcpHeader)
{ // Any packet without SYN and MP_CAPABLE is not being processed!
  NS_LOG_FUNCTION(this << mptcpHeader);
  NS_ASSERT(remoteToken == 0 && mpEnabled == false);
  vector<TcpOptions*> mp_options = mptcpHeader.GetOptions ();
  uint8_t flags = mptcpHeader.GetFlags ();
  TcpOptions *opt;
  bool hasSyn = flags & TcpHeader::SYN;
  for (uint32_t j = 0; j < mp_options.size (); j++)
    {
      opt = mp_options[j];
      if ((opt->optName == OPT_MPC) && hasSyn && (mpRecvState == MP_NONE))
        { // SYN+ACK would be send later on by ProcessSynRcvd(...)
          mpRecvState = MP_MPC;
          mpEnabled = true;
          remoteToken = ((OptMultipathCapable *) opt)->senderToken;
          if (remoteToken == 0)
            NS_ASSERT(remoteToken != 0); // Correct condition
          return true;
        }
      else
        {
          NS_LOG_UNCOND("[" << m_node->GetId() << "] Wrong option is received -> RETURN. MptcpHeader: " << mptcpHeader);
        }
    }
  return false; // If no option MP_CAPABLE is found -> RETURN then ForwardUp() should RETURN too!
}

// Read options from incoming packets
bool
MpTcpSocketBase::ReadOptions (uint8_t sFlowIdx, Ptr<Packet> pkt, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION(this << (int)sFlowIdx << mptcpHeader);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  vector<TcpOptions*> options = mptcpHeader.GetOptions ();
  uint8_t flags = mptcpHeader.GetFlags ();
  TcpOptions *opt;
  bool hasSyn = flags & TcpHeader::SYN;
  bool TxAddr = false;
  for (uint32_t j = 0; j < options.size (); j++)
    {
      opt = options[j];
      if ((opt->optName == OPT_MPC) && hasSyn && (mpRecvState == MP_NONE))
        { // SYN+ACK would be send later on by ProcessSynRcvd(...)
          mpRecvState = MP_MPC;
          mpEnabled = true;
          remoteToken = ((OptMultipathCapable *) opt)->senderToken;
          NS_ASSERT(remoteToken != 0);
          NS_ASSERT(client);
        }
      else if ((opt->optName == OPT_JOIN) && hasSyn)
        {
          OptJoinConnection * optJoin = (OptJoinConnection *) opt;
          if ((mpSendState == MP_ADDR) && (localToken == optJoin->receiverToken))
            { // SYN+ACK would be send later on by ProcessSynRcvd(...)
              // Join option is sent over the path (couple of addresses) not already in use
              NS_LOG_UNCOND("Server receive new subflow!");
            }
        }
      else if ((opt->optName == OPT_ADDR) && (mpRecvState == MP_MPC))
        {
          // Receiver store sender's addresses information and send back its addresses.
          // If there are several addresses to advertise then multiple OPT_ADDR would be attached to the TCP Options.
          MpTcpAddressInfo * addrInfo = new MpTcpAddressInfo ();
          addrInfo->addrID = ((OptAddAddress *) opt)->addrID;
          addrInfo->ipv4Addr = ((OptAddAddress *) opt)->addr;
          remoteAddrs.insert (remoteAddrs.end (), addrInfo);
          TxAddr = true;
        }
      else if (opt->optName == OPT_REMADR)
        { // not implemented yet
          NS_LOG_WARN(this << "ReadOption-> OPT_REMADR is not implemented yet");
        }
      else if (opt->optName == OPT_DSN)
        { // not implemented yet
          NS_LOG_LOGIC(this << " ReadOption-> OPT_DSN -> we'll deal with it later on");
        }
      else if (hasSyn)
        { // incoming packet has syn but without proper mptcp option
          // TODO Should send RST here as remoteToken is not received...
        }
    }
  if (TxAddr == true)
    {
      mpRecvState = MP_ADDR;
      // If addresses did not send yet then advertise them...
      if (mpSendState != MP_ADDR)
        {
          NS_LOG_DEBUG(Simulator::Now().GetSeconds()<< "---------------------- AdvertiseAvailableAddresses By Server ---------------------");
          NS_ASSERT(pathManager == FullMesh);
          AdvertiseAvailableAddresses (); // this is what the receiver has to do
          return false;
        }
      // If addresses already sent then initiate subflows...
      else if (mpSendState == MP_ADDR)
        {
          NS_ASSERT(pathManager == FullMesh);
          InitiateSubflows ();  // this is what the initiator has to do
          return false;
        }
    }
  return true;
}

/** Received a packet upon ESTABLISHED state. This function is mimicking the
 role of tcp_rcv_established() in tcp_input.c in Linux kernel. */
void
MpTcpSocketBase::ProcessEstablished (uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << mptcpHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mptcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  // Different flags are different events
  if (tcpflags == TcpHeader::ACK)
    {
      ReceivedAck (sFlowIdx, packet, mptcpHeader);
    }
  else if (tcpflags == TcpHeader::SYN)
    { // Received SYN, old NS-3 behaviour is to set state to SYN_RCVD and
      // respond with a SYN+ACK. But it is not a legal state transition as of
      // RFC793. Thus this is ignored.
    }
  else if (tcpflags == (TcpHeader::SYN | TcpHeader::ACK))
    { // No action for received SYN+ACK, it is probably a duplicated packet
    }
  else if (tcpflags == TcpHeader::FIN || tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
    { // Received FIN or FIN+ACK, bring down this socket nicely
      PeerClose (sFlowIdx, packet, mptcpHeader);
    }
  else if (tcpflags == 0)
    { // No flags means there is only data
      ReceivedData (sFlowIdx, packet, mptcpHeader);
//      if (m_rxBuffer.Finished ())
//        {
//          PeerClose (packet, mptcpHeader);
//        }
    }
  else
    { // Received RST or the TCP flags is invalid, in either case, terminate this socket
      if (tcpflags != TcpHeader::RST)
        { // this must be an invalid flag, send reset
          NS_LOG_LOGIC ("Illegal flag " << tcpflags << " received. Reset packet is sent."); //
          NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") Bad FtcpFlag received - SendRST");
          cout << Simulator::Now ().GetSeconds () << " [" << m_node->GetId () << "] (" << (int) sFlowIdx << ") {" << flowId
              << "} SendRST(ProcessEstablished)" << endl;
          SendRST (sFlowIdx);
        }
      CloseAndNotifyAllSubflows ();
    }
}

void
MpTcpSocketBase::ProcessListen (Ptr<Packet> packet, const TcpHeader& mptcpHeader, const Address& fromAddress,
                                const Address& toAddress)
{
  NS_LOG_FUNCTION (this << mptcpHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mptcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  // Fork a socket if received a SYN. Do nothing otherwise.
  // C.f.: the LISTEN part in tcp_v4_do_rcv() in tcp_ipv4.c in Linux kernel
  if (tcpflags != TcpHeader::SYN)
    {
      return;
    }

  // Call socket's notify function to let the server app know we got a SYN
  // If the server app refuses the connection, do nothing
  if (!NotifyConnectionRequest (fromAddress))
    {
      NS_LOG_ERROR("Server refuse the incoming connection!");
      return;
    }

  // Clone the socket, simulate Fork()
  //Ptr<MpTcpSocketBase> newSock = CopyObject<MpTcpSocketBase>(this);
  Ptr<MpTcpSocketBase> newSock = DynamicCast<MpTcpSocketBase> (Fork ());
  //NS_LOG_UNCOND ("Clone new MpTcpSocketBase new connection. ListenerSocket " << this << " AcceptedSocket "<< newSock);
  Simulator::ScheduleNow (&MpTcpSocketBase::CompleteFork, newSock, packet, mptcpHeader, fromAddress, toAddress);
}

void
MpTcpSocketBase::CompleteFork (Ptr<Packet> p, const TcpHeader& mptcpHeader, const Address& fromAddress, const Address& toAddress)
{
  NS_LOG_FUNCTION(this);
  // In closed object following conditions should be true!
  server = true;

  // Get port and address from peer (connecting host)
  if (InetSocketAddress::IsMatchingType (toAddress))
    {
      m_endPoint = m_tcp->Allocate (InetSocketAddress::ConvertFrom (toAddress).GetIpv4 (),
                                    InetSocketAddress::ConvertFrom (toAddress).GetPort (),
                                    InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                                    InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
    }
  NS_ASSERT(InetSocketAddress::ConvertFrom (toAddress).GetIpv4 () == m_localAddress);
  NS_ASSERT(InetSocketAddress::ConvertFrom (toAddress).GetPort () == m_localPort);
  NS_ASSERT(InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 () == m_remoteAddress);
  NS_ASSERT(InetSocketAddress::ConvertFrom (fromAddress).GetPort () == m_remotePort);

  // We only setup destroy callback for MPTCP connection's endPoints, not on subflows endpoints.
  SetupCallback ();
  //m_tcp->m_sockets.push_back(this); // TMP REMOVE

  // Create new master subflow (master subsock) and assign its endpoint to the connection endpoint
  Ptr<MpTcpSubFlow> sFlow = CreateObject<MpTcpSubFlow> ();
  sFlow->routeId = (subflows.size () == 0 ? 0 : subflows[subflows.size () - 1]->routeId + 1);
  sFlow->sAddr = m_localAddress; //m_endPoint->GetLocalAddress();
  sFlow->sPort = m_localPort;    //m_endPoint->GetLocalPort();
  sFlow->dAddr = m_remoteAddress;
  sFlow->dPort = m_remotePort;   // TODO ? I guess m_remotePort would be used here!
  sFlow->MSS = segmentSize;
  sFlow->state = SYN_RCVD;
  sFlow->cnTimeout = m_cnTimeout;
  sFlow->cnRetries = m_cnRetries;
  sFlow->cnCount = sFlow->cnRetries;
  sFlow->rtt->SetG (m_g);
  sFlow->m_endPoint = m_endPoint; // This is master subsock, its endpoint is the same as connection endpoint.
  NS_LOG_INFO ("("<< (int)sFlow->routeId<<") LISTEN -> SYN_RCVD");
  subflows.insert (subflows.end (), sFlow);
  sFlow->RxSeqNumber = (mptcpHeader.GetSequenceNumber ()).GetValue () + 1; //Set the subflow sequence number and send SYN+ACK
  NS_LOG_DEBUG("CompleteFork -> RxSeqNb: " << sFlow->RxSeqNumber << " highestAck: " << sFlow->highestAck);
  SendEmptyPacket (sFlow->routeId, TcpHeader::SYN | TcpHeader::ACK);

  // Update currentSubflow in case close just after 3WHS.
  currentSublow = sFlow->routeId;
  //NS_LOG_UNCOND("CompleteFork -> receivingBufferSize: " << recvingBuffer->bufMaxSize); //
  NS_LOG_INFO(this << "  MPTCP connection is initiated (Receiver): " << sFlow->sAddr << ":" << sFlow->sPort << " -> " << sFlow->dAddr << ":" << sFlow->dPort);
}

/** Received a packet upon LISTEN state. */
void
MpTcpSocketBase::ProcessListen (uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader, const Address& fromAddress,
                                const Address& toAddress)
{
  NS_LOG_FUNCTION (m_node->GetId() << mptcpHeader);
  uint8_t tcpflags = mptcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  /*
   * Here the SYN is only flag that is expected to receives in normal operation.
   * But, it might also be possible to get SYN with data piggyback when MPTCP has already an ESTABLISHED master subflow.
   */
  if (tcpflags == TcpHeader::SYN)
    { // Receiver got new SYN...Sends SYN+ACK.
      // This is a valid condition when receiver got SYN with MP_JOIN from sender and create new subflow with LISTEN state.
      NS_LOG_INFO(" (" << sFlow->routeId << ") " << TcpStateName[sFlow->state] << " -> SYN_RCVD");
      sFlow->state = SYN_RCVD;
      sFlow->RxSeqNumber = (mptcpHeader.GetSequenceNumber ()).GetValue () + 1;
      NS_ASSERT(sFlow->highestAck == mptcpHeader.GetAckNumber ().GetValue ());
      SendEmptyPacket (sFlowIdx, TcpHeader::SYN | TcpHeader::ACK);
    }
  else if (tcpflags == TcpHeader::ACK)
    {
      NS_FATAL_ERROR("Subflow state is LISTEN, how come it receives ACK flag...");
    }

  if (tcpflags == 0 && subflows.size () > 1)
    {      // Slave subflows can receive SYN flag piggyback data packet.
      ReceivedData (sFlowIdx, packet, mptcpHeader);
    }
}

/** Received a packet upon SYN_SENT */
void
MpTcpSocketBase::ProcessSynSent (uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << mptcpHeader);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mptcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  // Execute a action based on tcpflags
  if (tcpflags == 0)
    { // Bare data, accept it and move to ESTABLISHED state. This is not a normal behaviour. Remove this?
      NS_ASSERT(tcpflags != 0);
    }
  else if (tcpflags == TcpHeader::ACK)
    {
      NS_ASSERT(tcpflags != TcpHeader::ACK);
    }
  else if (tcpflags == TcpHeader::SYN)
    {
      NS_ASSERT(tcpflags != TcpHeader::SYN);
    }
  else if (tcpflags == (TcpHeader::SYN | TcpHeader::ACK))
    { // Handshake completed for sender... Send final ACK
      //NS_LOG_UNCOND("---------------------- HandShake is Completed in ClientSide ----------------------" << subflows.size());
      if (!m_connected)
        { // This function only excute for initial subflow since when it has established then MPTCP connection is already established!!
          m_connected = true;
          m_endPoint->SetPeer (m_remoteAddress, m_remotePort); // TODO Is this needed at all?
          fLowStartTime = Simulator::Now ().GetSeconds ();     // It seems to be in right location for FCT!!
          if ((m_ratePlotCl && (flowType.compare ("Short") != 0)) || (m_largePlotting && (flowType.compare ("Short") != 0)))
            RateTracerCl ();
        }NS_LOG_INFO("(" << sFlow->routeId << ") "<< TcpStateName[sFlow->state] << " -> ESTABLISHED");
      sFlow->state = ESTABLISHED;
      sFlow->retxEvent.Cancel ();
      if ((m_largePlotting && (flowType.compare ("Large") == 0)) || (m_shortPlotting && (flowType.compare ("Short") == 0)))
        sFlow->StartTracing ("cWindow");
      sFlow->rtt->Init (mptcpHeader.GetAckNumber ());
      sFlow->initialSequnceNumber = (mptcpHeader.GetAckNumber ().GetValue ());
      NS_LOG_INFO("(" <<sFlow->routeId << ") InitialSeqNb of data packet should be --->>> " << sFlow->initialSequnceNumber << " Cwnd: " << sFlow->cwnd);
      sFlow->RxSeqNumber = (mptcpHeader.GetSequenceNumber ()).GetValue () + 1;
      sFlow->highestAck = std::max (sFlow->highestAck, mptcpHeader.GetAckNumber ().GetValue () - 1);
      sFlow->TxSeqNumber = mptcpHeader.GetAckNumber ().GetValue ();
      sFlow->maxSeqNb = sFlow->TxSeqNumber - 1;
      sFlow->m_highTxMark = sFlow->TxSeqNumber - 1;
      if ((m_ratePlotSf && (flowType.compare ("Short") != 0)) || (m_largePlotting && (flowType.compare ("Short") != 0)))
        { //sFlow->lastTxSeqNumer = sFlow->TxSeqNumber;
          sFlow->RateTracerSf (m_rateInterval, flowCompletionTime);
        }
      Time estimate;
      estimate = Seconds (1.5);
      sFlow->rtt->SetCurrentEstimate (estimate);

      SendEmptyPacket (sFlowIdx, TcpHeader::ACK);

      // Advertise available addresses...
      if (addrAdvertised == false)
        {
          NS_LOG_WARN("---------------------- AdvertiseAvailableAddresses By Client ---------------------");
          switch (pathManager)
            {
          case Default:
            // No address advertisement
            break;
          case FullMesh:
            // Address need to be advertised
            AdvertiseAvailableAddresses ();
            break;
          case NdiffPorts:
            // New subflow can be initiated based on random source ports
            InitiateMultipleSubflows ();
            break;
          default:
            break;
            }
          addrAdvertised = true;
        }

      if (m_state != ESTABLISHED)
        {
          m_state = ESTABLISHED;
          NotifyConnectionSucceeded ();
        }
      if (subflows.size () > 1)
        {
          SendPendingData (sFlowIdx); // in processSynSent()
        }
      //NS_LOG_UNCOND("ProcessSynSent -> SubflowsSize: " << subflows.size());
    } // end of else if (SYN/ACK)
  else
    { // Other in-sequence input
      if (tcpflags != TcpHeader::RST)
        { // When (1) rx of FIN+ACK; (2) rx of FIN; (3) rx of bad flags
          NS_LOG_LOGIC ("Illegal flag " << std::hex << static_cast<uint32_t> (tcpflags) << std::dec << " received. Reset packet is sent."); //
          NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") Bad FtcpFlag received - SendRST");
          cout << Simulator::Now ().GetSeconds () << " [" << m_node->GetId () << "] (" << (int) sFlowIdx << ") {" << flowId
              << "} SendRST(ProcessSynSent)" << endl;
          SendRST (sFlowIdx);
        }
      CloseAndNotifyAllSubflows ();
    }
}

/** Received a packet upon SYN_RCVD */
void
MpTcpSocketBase::ProcessSynRcvd (uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader, const Address& fromAddress,
                                 const Address& toAddress)
{
  NS_LOG_FUNCTION (this << mptcpHeader);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint8_t tcpflags = mptcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  if (tcpflags == 0 || (tcpflags == TcpHeader::ACK))
    { // handshake is completed nicely in the receiver.
      NS_LOG_INFO (" ("<< sFlow->routeId << ") " << TcpStateName[sFlow->state]<<" -> ESTABLISHED");
      sFlow->state = ESTABLISHED; // Subflow state is ESTABLISHED
      m_state = ESTABLISHED;      // NEED TO CONSIDER IT AGAIN....
      sFlow->connected = true;    // This means subflow is established
      sFlow->retxEvent.Cancel ();  // This would cancel ReTxTimer where it being setup when SYN is sent.
      sFlow->TxSeqNumber++;
      sFlow->maxSeqNb = sFlow->TxSeqNumber - 1;
      sFlow->m_highTxMark = sFlow->TxSeqNumber - 1;
      NS_ASSERT(sFlow->TxSeqNumber == mptcpHeader.GetAckNumber ().GetValue ());

      // Check MPTCP Connection status. If it is not yet ESTABLISHED then change it to true to indicate that.
      if (m_connected != true)
        {
          m_connected = true;
          NotifyNewConnectionCreated (this, m_remoteAddress);
        }
      //
      //NS_LOG_UNCOND ("---------------------- HandShake is Completed in ServerSide ----------------------" << subflows.size());
      if (tcpflags == 0)
        {
          NS_LOG_WARN(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << sFlow->routeId <<") It seems sender final ACK of 3WHS has been lost, but its first data packet has been received!!");
          ReceivedAck (sFlowIdx, packet, mptcpHeader);
        }
    }
  else if (tcpflags == TcpHeader::SYN)
    { // SYN/ACK sent might be lost, send SYN/ACK again.
      NS_LOG_INFO ("("<< sFlow->routeId << ") " << "SYN_RCVD -> SYN_RCVD");
      sFlow->RxSeqNumber = (mptcpHeader.GetSequenceNumber ()).GetValue () + 1;
      SendEmptyPacket (sFlowIdx, TcpHeader::SYN | TcpHeader::ACK);
    }
  else if (tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
    {
      NS_LOG_UNCOND("This is happens when FIN/ACK is received when connection is not established yet! - Need to be implemented");
      cout << Simulator::Now ().GetSeconds () << " [" << m_node->GetId () << "] (" << (int) sFlowIdx << ") {" << flowId
          << "} Flag{" << TcpFlagPrinter (tcpflags) << "}" << endl;
    }
  else if (tcpflags == (TcpHeader::FIN))
    {
      NS_LOG_UNCOND("This is happens when FIN is received when connection is not established yet! - Need to be implemented");
      cout << Simulator::Now ().GetSeconds () << " [" << m_node->GetId () << "] (" << (int) sFlowIdx << ") {" << flowId
           << "} Flag{" << TcpFlagPrinter (tcpflags) << "}" << endl;
    }
  else
    { // Other in-sequence input
      if (tcpflags != TcpHeader::RST)
        { // When (1) rx of SYN+ACK; (2) rx of FIN; (3) rx of bad flags
          NS_LOG_LOGIC ("Illegal flag " << tcpflags << " received. Reset packet is sent.");
//          if (m_endPoint)
//            {
//              m_endPoint->SetPeer(InetSocketAddress::ConvertFrom(fromAddress).GetIpv4(),
//                  InetSocketAddress::ConvertFrom(fromAddress).GetPort());
//            }
          NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << (int)sFlowIdx << ") Bad FtcpFlag received - SendRST");
          cout << Simulator::Now ().GetSeconds () << " [" << m_node->GetId () << "] (" << (int) sFlowIdx << ") {" << flowId
              << "} SendRST(ProcessSynRcvd)" << endl;
          SendRST (sFlowIdx); // Send RST if receive unexpected flag
        }
      CloseAndNotifyAllSubflows ();
    }
}

/** Received a packet upon CLOSE_WAIT, FIN_WAIT_1, or FIN_WAIT_2 states */
void
MpTcpSocketBase::ProcessWait (uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << sFlowIdx <<packet <<mptcpHeader);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mptcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  if (packet->GetSize () > 0 && tcpflags != TcpHeader::ACK)
    { // Bare data, accept it
      ReceivedData (sFlowIdx, packet, mptcpHeader);
    }
  else if (tcpflags == TcpHeader::ACK)
    { // Process the ACK, and if in FIN_WAIT_1, conditionally move to FIN_WAIT_2
      ReceivedAck (sFlowIdx, packet, mptcpHeader);
    }
  else if (tcpflags == TcpHeader::FIN || tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
    { // Got FIN, respond with ACK and move to next state
      NS_LOG_INFO( "("<<(int) sFlow->routeId << ") ProcessWait -> " << mptcpHeader);
      if (tcpflags & TcpHeader::ACK)
        { // Process the ACK first
          NS_LOG_INFO( "("<<(int)sFlow->routeId << ") ProcessWait -> ReceviedAck() sFlow->state: " << TcpStateName[sFlow->state]);
          ReceivedAck (sFlowIdx, packet, mptcpHeader);
        }
      sFlow->SetFinSequence (mptcpHeader.GetSequenceNumber ());
      NS_LOG_INFO ("("<<(int)sFlow->routeId<<") Accepted FIN at seq " << mptcpHeader.GetSequenceNumber () + SequenceNumber32 (packet->GetSize ()) << ", PktSize: " << packet->GetSize() << " {ProcessWait}");
    }
  else if (tcpflags == TcpHeader::SYN || tcpflags == (TcpHeader::SYN | TcpHeader::ACK))
    { // Duplicated SYN or SYN+ACK, possibly due to spurious retransmission
      NS_LOG_INFO("ProcessWait -> got SYN or SYN/ACK  " << mptcpHeader);
      return;
    }
  else
    { // This is a RST or bad flags
      NS_LOG_INFO("ProcessWait -> got RST or bad flags  " << mptcpHeader);
      if (tcpflags != TcpHeader::RST)
        {
          NS_LOG_LOGIC ("Illegal flag " << tcpflags << " received. Reset packet is sent.");
          cout << Simulator::Now ().GetSeconds () << " [" << m_node->GetId () << "] (" << (int) sFlowIdx << ") {" << flowId
              << "} SendRST(ProcessWait)" << endl;
          SendRST (sFlowIdx);
        }
      CloseAndNotifyAllSubflows ();
      return;
    }
  // Check if the close responder sent an in-sequence FIN, if so, respond ACK
  if ((sFlow->state == FIN_WAIT_1 || sFlow->state == FIN_WAIT_2) && sFlow->Finished ())
    {
      if (sFlow->state == FIN_WAIT_1)
        {
          NS_LOG_INFO ("("<< (int) sFlowIdx <<") FIN_WAIT_1 -> CLOSING {ProcessWait}");
          sFlow->state = CLOSING;
          if (sendingBuffer.Empty () && sFlow->mapDSN.size () == 0
              && mptcpHeader.GetAckNumber ().GetValue () == sFlow->highestAck + 1)
            { // This ACK corresponds to the FIN sent
              TimeWait (sFlowIdx);
            }
        }
      else if (sFlow->state == FIN_WAIT_2)
        {
          TimeWait (sFlowIdx);
        }
      SendEmptyPacket (sFlowIdx, TcpHeader::ACK);
    }
}

/** Move TCP to Time_Wait state and schedule a transition to Closed state */
void
MpTcpSocketBase::TimeWait (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION((int) sFlowIdx);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  NS_LOG_INFO ("("<< (int) sFlow->routeId << ") "<<TcpStateName[sFlow->state] << " -> TIME_WAIT {TimeWait}");
  sFlow->state = TIME_WAIT;
  CancelAllTimers (sFlowIdx);
  // Move from TIME_WAIT to CLOSED after 2*MSL. Max segment lifetime is 2 min according to RFC793, p.28
  //sFlow->m_timewaitEvent = Simulator::Schedule(Seconds(2 * 120), &MpTcpSocketBase::CloseMultipathConnection, this);
  sFlow->m_timewaitEvent = Simulator::Schedule (Simulator::Now (), &MpTcpSocketBase::CloseMultipathConnection, this);
}

void
MpTcpSocketBase::CancelAllTimers (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION((int) sFlowIdx);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  sFlow->retxEvent.Cancel ();
  sFlow->m_lastAckEvent.Cancel ();
  sFlow->m_timewaitEvent.Cancel ();
  NS_LOG_LOGIC( "(" << (int)sFlow->routeId<<")" << "CancelAllTimers");
}

void
MpTcpSocketBase::CancelAllSubflowTimers (void)
{
  NS_LOG_FUNCTION_NOARGS();
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[i];
      if (sFlow->state != CLOSED)
        {
          sFlow->retxEvent.Cancel ();
          sFlow->m_lastAckEvent.Cancel ();
          sFlow->m_timewaitEvent.Cancel ();
          NS_LOG_INFO("CancelAllSubflowTimers() -> Subflow:" << sFlow->routeId);
        }
    }
}

void
MpTcpSocketBase::ProcessLastAck (uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << mptcpHeader);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  NS_LOG_INFO("("<< (int)sFlowIdx << ") ProcessLastAck -> HeaderSeqNb: " << mptcpHeader.GetSequenceNumber() << " == sFlow->RxSeqNb: " << sFlow->RxSeqNumber);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mptcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  if (tcpflags == 0)
    {
      ReceivedData (sFlowIdx, packet, mptcpHeader);
    }
  else if (tcpflags == TcpHeader::ACK)
    {
      if (mptcpHeader.GetSequenceNumber () == SequenceNumber32 (sFlow->RxSeqNumber))
        { // This ACK corresponds to the FIN sent. This socket closed peacefully.
          NS_LOG_INFO("("<<(int) sFlow->routeId << ") ProcessLastAck -> This ACK corresponds to the FIN sent -> CloseAndNotify (" << (int)sFlowIdx << ")");
          CloseAndNotify (sFlowIdx);

        }
    }
  else if (tcpflags == TcpHeader::FIN)
    { // Received FIN again, the peer probably lost the FIN+ACK
      SendEmptyPacket (sFlowIdx, TcpHeader::FIN | TcpHeader::ACK);
    }
  else if (tcpflags == (TcpHeader::FIN | TcpHeader::ACK) || tcpflags == TcpHeader::RST)
    {
      CloseAndNotify (sFlowIdx);
    }
  else
    { // Received a SYN or SYN+ACK or bad flags
      NS_LOG_INFO ("Illegal flag " << tcpflags << " received. Reset packet is sent.");
      cout << Simulator::Now ().GetSeconds () << " [" << m_node->GetId () << "] (" << (int) sFlowIdx << ") {" << flowId
          << "} SendRST(ProcessLastAck)" << endl;
      SendRST (sFlowIdx);
      CloseAndNotifyAllSubflows ();
    }
}

// Receipt of new packet
void
MpTcpSocketBase::ReceivedData (uint8_t sFlowIdx, Ptr<Packet> p, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << mptcpHeader);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint32_t expectedSeq = sFlow->RxSeqNumber;
  uint32_t Seq = mptcpHeader.GetSequenceNumber ().GetValue ();
  vector<TcpOptions*> options = mptcpHeader.GetOptions ();
  TcpOptions* opt;
  bool stored = true;
  for (uint32_t i = 0; i < options.size (); i++)
    {
      opt = options[i];
      if (opt->optName == OPT_DSN)
        {
          OptDataSeqMapping* optDSN = (OptDataSeqMapping*) opt;
          NS_ASSERT(optDSN->subflowSeqNumber == Seq);
          if (optDSN->subflowSeqNumber == sFlow->RxSeqNumber)
            { /* Received packet is in-sequence at sub-flow level. Now check connection level? */
              if (optDSN->dataSeqNumber == nextRxSequence)
                {/** Received packet is in-sequence at connection level but in-order at sub-flow level **/
                  uint32_t amountRead = recvingBuffer.ReadPacket (p, optDSN->dataLevelLength);
                  if (amountRead == 0)
                    {
                      NS_FATAL_ERROR("I don't see any reason to trigger this condition, at least in current implementation");
                      return;
                    }
                  NS_ASSERT(amountRead == optDSN->dataLevelLength && optDSN->dataLevelLength == p->GetSize ());
                  sFlow->RxSeqNumber += amountRead;
                  // Increasing it would not hurt but it is essential for MMPTCP
                  sFlow->highestAck = std::max (sFlow->highestAck, (mptcpHeader.GetAckNumber ()).GetValue () - 1);
                  nextRxSequence += amountRead;
                  ReadUnOrderedData ();
                  //SendAccumulativeAck(sFlowIdx);
                  if (expectedSeq < sFlow->RxSeqNumber)
                    {
                      NotifyDataRecv ();
                    }
                  SendEmptyPacket (sFlowIdx, TcpHeader::ACK);

                  if (sFlow->Finished () && (mptcpHeader.GetFlags () & TcpHeader::FIN) == 0)
                    { // If we received FIN before and now completed all "holes" in RX buffer, invoke peer close
                      NS_LOG_INFO("("<< (int) sFlowIdx <<") Last data packet recovered, now already received FIN is got in-sequence!");
                      DoPeerClose (sFlowIdx);
                      return;
                    }
                }
              else if (optDSN->dataSeqNumber > nextRxSequence) // there is a gap in dataSeqNumber
                { /** Received packet is out of sequence at connection level but in-order at sub-flow level **/
                  stored = StoreUnOrderedData (
                      new DSNMapping (sFlowIdx, optDSN->dataSeqNumber, optDSN->dataLevelLength, optDSN->subflowSeqNumber,
                                      mptcpHeader.GetAckNumber ().GetValue ()/*, p*/));
                  // For allowing sub-flow to progress, RxSeqNb should be advanced even though packet is not in-order of connection level.
                  if (stored)
                    {
                      NS_ASSERT(optDSN->subflowSeqNumber == sFlow->RxSeqNumber);
                      sFlow->RxSeqNumber += optDSN->dataLevelLength;
                      sFlow->highestAck = std::max (sFlow->highestAck, (mptcpHeader.GetAckNumber ()).GetValue () - 1);

                    }
                  // We need to send ACK here to indicate that a packet leaves a network and signaling to sender that which sequence number is expected to receive at sub-flow level.
                  SendEmptyPacket (sFlowIdx, TcpHeader::ACK);
                }
              else
                { /** Received packet is duplicated in connection level! */
                  NS_ASSERT(optDSN->dataSeqNumber < nextRxSequence);
                  NS_FATAL_ERROR("This functionality is not yet implemented!"); NS_LOG_WARN(this << "Duplicated segment received at connection level so it should be rejected!");
                  SendEmptyPacket (sFlowIdx, TcpHeader::ACK);
                }
            }
          else if (optDSN->subflowSeqNumber > sFlow->RxSeqNumber)
            { /* Received packet is out of order at sub-flow level */
              // This condition might occurs when a packet get drop...Does this condition mean that packet should be out of order at connection level? YES
              NS_ASSERT(optDSN->dataSeqNumber > nextRxSequence);
              StoreUnOrderedData (
                  new DSNMapping (sFlowIdx, optDSN->dataSeqNumber, optDSN->dataLevelLength, optDSN->subflowSeqNumber,
                                  mptcpHeader.GetAckNumber ().GetValue ()/*, p*/));
              SendEmptyPacket (sFlowIdx, TcpHeader::ACK); // We need to send ACK regardless of whether segment has already stored in unOrdered or not!
            }
          else if (optDSN->subflowSeqNumber < sFlow->RxSeqNumber)
            { /* Received packet is duplicated at sub-flow level. It should be rejected!*/
              NS_LOG_INFO("Data received is duplicated in Subflow Layer so it has been rejected! subflowSeq: " << optDSN->subflowSeqNumber << " dataSeq: " << optDSN->dataSeqNumber);
              SendEmptyPacket (sFlowIdx, TcpHeader::ACK);  // Ask for next expected sub-flow sequence number to receive.
            }
          else
            NS_FATAL_ERROR_NO_MSG()
            ; // There should not be any other condition!
        } // end of if clause
      else
        NS_FATAL_ERROR(
            "ReceivedData() has called when there is no DSN option in the packet - Currently only DSN option is sent in each data packet!");
    } // end of for loop over TCP options
}

void
MpTcpSocketBase::SendAccumulativeAck (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION(this);
  vector<Ptr<MpTcpSubFlow> >::iterator it;
  for (it = subflows.begin (); it != subflows.end (); ++it)
    {
      if ((*it)->routeId != sFlowIdx && ((*it)->AccumulativeAck) && (*it)->m_gotFin)
        {
          NS_LOG_UNCOND("SendAccumulativeAck -> Subflow: " << (int)(*it)->routeId << " Called sub-flow: " << (int) sFlowIdx);
          (*it)->AccumulativeAck = false;
          SendEmptyPacket ((*it)->routeId, TcpHeader::ACK);
        }
    }
}
/** Process the newly received ACK */
void
MpTcpSocketBase::ReceivedAck (uint8_t sFlowIdx, Ptr<Packet> packet, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << sFlowIdx << mptcpHeader);

  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint32_t ack = (mptcpHeader.GetAckNumber ()).GetValue ();
  sFlow->g_AckSeqNumber = ack;

  if ((m_isDCTCPEnabled && sFlow->state == ESTABLISHED) || m_dctcpFastReTxRecord)
    { // Danger: m_dctcpFastReTxRecord should off in normal run
      if (m_ADCT && nextTxSequence >= m_ADCTthresh && m_ADCTcontrol)
        {
          cout << "G(" << m_g << " -> " << m_ADCTg << ")" << endl;
          m_g = m_ADCTg;
          m_ADCTcontrol = false;
        }
      CalculateDCTCPAlpha (sFlowIdx, ack);
    }

#ifdef PLOT
  uint32_t tmp = ((ack - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->ACK.push_back (make_pair (Simulator::Now ().GetSeconds (), tmp));
#endif

  // Stop execution if TCPheader is not ACK at all.
  if (0 == (mptcpHeader.GetFlags () & TcpHeader::ACK))
    { // Ignore if no ACK flag
      //NS_ASSERT(3!=3);
    }
  // Received ACK. Compare the ACK number against highest unacked seqno.
  else if (ack <= sFlow->highestAck + 1)
    {
      NS_LOG_LOGIC ("This acknowlegment" << mptcpHeader.GetAckNumber () << "do not ack the latest data in subflow level");
      list<DSNMapping *>::iterator current = sFlow->mapDSN.begin ();
      list<DSNMapping *>::iterator next = sFlow->mapDSN.begin ();
      while (current != sFlow->mapDSN.end ())
        {
          ++next;
          DSNMapping *ptrDSN = *current;
          // All segments before ackSeqNum should be removed from the mapDSN list.
          if (ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength <= ack)
            { // Optional task ...
              //next = sFlow->mapDSN.erase(current);
              //delete ptrDSN;
            }
          // There is a sent segment with subflowSN equal to ack but the ack is smaller than already receveid acked!
          else if ((ptrDSN->subflowSeqNumber == ack) && (ack < sFlow->highestAck + 1))
            { // Case 1: Old ACK, ignored.
              NS_LOG_WARN ("Ignored ack of " << mptcpHeader.GetAckNumber());
              NS_ASSERT(3 != 3);
              break;
            }
          // There is a sent segment with requested SequenceNumber and ack is for first unacked byte!!
          else if ((ptrDSN->subflowSeqNumber == ack) && (ack == sFlow->highestAck + 1))
            { // Case 2: Potentially a duplicated ACK, so ack should be smaller than nextExpectedSN to send.
              if (ack < sFlow->TxSeqNumber)
                {
                  //NS_LOG_ERROR(Simulator::Now().GetSeconds()<< " [" << m_node->GetId()<< "] Duplicated ack received for SeqgNb: " << ack << " DUPACKs: " << sFlow->m_dupAckCount + 1);
                  DupAck (sFlowIdx, ptrDSN);
                  break;
                }
              // otherwise, the ACK is precisely equal to the nextTxSequence
              NS_ASSERT(ack <= sFlow->TxSeqNumber);
              break;
            }
          current = next;
        }
    }
  else if (ack > sFlow->highestAck + 1)
    { // Case 3: New ACK, reset m_dupAckCount and update m_txBuffer (DSNMapping List)
      NS_LOG_WARN ("New ack of " << mptcpHeader.GetAckNumber ());
      NewAckNewReno (sFlowIdx, mptcpHeader, 0);
      sFlow->m_dupAckCount = 0;
    }
  // If there is any data piggy-backed, store it into m_rxBuffer
  if (packet->GetSize () > 0)
    {
      NS_LOG_WARN(this << " ReceivedAck -> There is data piggybacked, deal with it...");
      ReceivedData (sFlowIdx, packet, mptcpHeader);
    }
  // Find last data acked ... for generating output file!
  if (!server)
    IsLastAck ();

}

void
MpTcpSocketBase::AddPacketTag (Ptr<Packet> p, PacketTag_t pt)
{
  EctTag ect;
  CeTag ce;
  EceTag ece;
  ControlTag ct;
  switch (pt)
    {
  case ECT_TAG:
    ect.SetEct(1);
    p->AddPacketTag(ect);
    break;
  case CE_TAG:
    ce.SetCe(1);
    p->AddPacketTag(ce);
  break;
  case ECE_TAG:
    ece.SetEce (1);
    p->AddPacketTag (ece);
  break;
  case CP_TAG:
    ct.SetControlPkt(1);
    p->AddPacketTag(ct);
    break;
  default:
    break;
    }
}

void
MpTcpSocketBase::AddEcmpTag (Ptr<Packet> p, uint8_t sFlowIdx)
{
  EcmpTag ecmp;
  ecmp.SetEcmp(m_initialRand + sFlowIdx);
  p->AddPacketTag(ecmp);
  //cout << "Rand(" << (int)m_initialRand << ") sFlowIdx(" << (int)sFlowIdx << ") EcmpIdx(" << (int)ecmp.GetEcmp() << ")" << endl;
}

bool 
MpTcpSocketBase::IncastDetected () 
{
  return m_CongestionRound >= m_CongestionThreshold;
}

void
MpTcpSocketBase::ShouldSuppressSubflows (uint8_t sFlowIdx)
{
      assert(sFlowIdx == 0);
      uint32_t lowCongestionWindowSubflows = 0; 
      for (uint16_t idx = 0; idx < subflows.size (); idx++)
        { 
          Ptr<MpTcpSubFlow> sFlow = subflows[idx];
          if (IncastDetected() && idx == 0) {
              uint32_t sFlowCwnd = (uint32_t)(sFlow->cwnd.Get () / sFlow->MSS) ; 
              if (sFlowCwnd > m_cwndMin && sFlow->dctcp_last_fraction == 0  && !(sFlow->maxSeqNb > sFlow->TxSeqNumber - 1) && sFlow->m_inFastRec == false)
                  m_CongestionReDoCounter++;
              else
                  m_CongestionReDoCounter = 0;

              if (m_CongestionReDoCounter >= m_CongestionExitThreshold)
                  {
                    m_CongestionReDoCounter = 0; 
                    m_CongestionExitHits++;
                    m_CongestionRound = 0;     
                  }
          }
          if (sFlow->state == ESTABLISHED)
            { 
              uint32_t sFlowCwnd = (uint32_t)(sFlow->cwnd.Get () / sFlow->MSS) ; 
              if ((sFlowCwnd == m_cwndMin) &&  !(sFlow->maxSeqNb > sFlow->TxSeqNumber - 1) && sFlow->m_inFastRec == false)
                  lowCongestionWindowSubflows++;
                
            }
          if (idx == subflows.size () - 1) {
            if (lowCongestionWindowSubflows >= subflows.size ())  
                m_CongestionRound = 0;
            else 
                m_CongestionRound++;
            if (IncastDetected())
                m_CongestionEnterHits++;
          }

        }
       
}

void
MpTcpSocketBase::CalculateDCTCPAlpha (uint8_t sFlowIdx, uint32_t ack)
{
  NS_LOG_FUNCTION((int) sFlowIdx << ack);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  // Tracking total and marked ACK packets
  sFlow->dctcp_total++;
  if (m_eceBit > 0)
    {
      sFlow->dctcp_marked++;
      sFlow->curEcnState = true;
#ifdef PLOT_DCTCP
      uint32_t tmp = ((ack - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
      sFlow->ECN_ECHO.push_back (make_pair (Simulator::Now ().GetSeconds (), tmp));
#endif
    }

  if (ack > sFlow->dctcp_alpha_update_seq)
    {
      double temp_alpha;
      if (sFlow->dctcp_total > 0)
        {
          temp_alpha = ((double) sFlow->dctcp_marked) / sFlow->dctcp_total;
        }
      else
        {
          temp_alpha = 0.0;
        }
      sFlow->dctcp_last_fraction = temp_alpha; // for ploting
      sFlow->dctcp_alpha = (1 - m_g) * sFlow->dctcp_alpha + m_g * temp_alpha;

      if (sFlow->dctcp_alpha > 1.0)
        sFlow->dctcp_alpha = 1.0;

      if (m_dctcpFastAlpha)
        sFlow->dctcp_alpha = sFlow->dctcp_last_fraction;

      if (m_isAdaptiveSubflow && (int)sFlowIdx == 0 && maxSubflows >= 2)
        ShouldSuppressSubflows(sFlowIdx);

      sFlow->dctcp_marked = 0;
      sFlow->dctcp_total = 0;
      sFlow->dctcp_alpha_update_seq = sFlow->TxSeqNumber;
      sFlow->curEcnState = m_eceBit > 0 ? true : false;
#ifdef PLOT_DCTCP
      sFlow->DCTCP_ALPHA.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->dctcp_alpha));
      sFlow->DCTCP_FRACTION.push_back (make_pair (Simulator::Now ().GetSeconds (), temp_alpha));
      uint32_t pktNumber = ((ack - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
      sFlow->BEG.push_back (make_pair (Simulator::Now ().GetSeconds (), pktNumber)); // ROUND for ECN and DCTCP
#endif
    }
}

void
MpTcpSocketBase::SetSegSize (uint32_t size)
{
  segmentSize = size;
  NS_ABORT_MSG_UNLESS(m_state == CLOSED, "Cannot change segment size dynamically.");
}

uint32_t
MpTcpSocketBase::GetSegSize (void) const
{
  return segmentSize;
}

// This function only called by SendPendingData() in our implementation!
int
MpTcpSocketBase::SendDataPacket (uint8_t sFlowIdx, uint32_t size, bool withAck)
{
  NS_LOG_FUNCTION (this << (uint32_t)sFlowIdx << size << withAck);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  Ptr<Packet> p = 0;
  DSNMapping * ptrDSN = 0;
  uint32_t packetSize = size;
  bool guard = false;
  /*
   * If timeout happens then TxSeqNumber would be shifted down to the seqNb after highestAck,
   * Note that 'maxSeqNb' would be still related to maxSeqNb ever sent.
   * here we can conclude that when maxSeqNb is bigger than TxSeqNumber -1, timeout has happened!
   * So next packet to send should be from subflowBuffer (mapDSN) instead of connection buffer (sendingBuffer),
   * In other situations 'maxSeqNb' should be equal to TxSeqNumber -1.
   * Boolean 'guard' is true only if packet going out is from subflow buffer!
   */
  if (sFlow->maxSeqNb > sFlow->TxSeqNumber - 1)
    {
      uint32_t IterNumber = 0;
      for (list<DSNMapping *>::iterator it = sFlow->mapDSN.begin (); (it != sFlow->mapDSN.end () && guard == false); ++it)
        { // Look for match a segment from subflow's buffer where it is matched with TxSeqNumber
          IterNumber++;
          DSNMapping * ptr = *it;
          if (ptr->subflowSeqNumber == sFlow->TxSeqNumber)
            {
              ptrDSN = ptr;
              //p = Create<Packet>(ptrDSN->packet, ptrDSN->dataLevelLength);
              p = Create<Packet> (ptrDSN->dataLevelLength);
              packetSize = ptrDSN->dataLevelLength;
              guard = true;
              NS_LOG_LOGIC(Simulator::Now().GetSeconds() <<" A segment matched from subflow buffer. Its size is "<< packetSize << " IterNumInMapDSN: " << IterNumber <<" maxSeqNb: " << sFlow->maxSeqNb << " TxSeqNb: " << sFlow->TxSeqNumber << " FastRecovery: " << sFlow->m_inFastRec << " SegNb: " << ptrDSN->subflowSeqNumber); //
              break;
            }
        }
      if (p == 0)
        {
          NS_LOG_UNCOND("*** MaxSeq: "<< sFlow->maxSeqNb << " sFlow->TxSeq: " << sFlow->TxSeqNumber);
          NS_ASSERT_MSG(p != 0, "Subflow is in timeout recovery but there is no match segment in mapDSN - Return -1 ?");
          return -1;
        }
    }
  else
    {
      NS_ASSERT_MSG(sFlow->maxSeqNb == sFlow->TxSeqNumber - 1,
                    " maxSN: " << sFlow->maxSeqNb << " TxSeqNb-1" << sFlow->TxSeqNumber -1);
    }

  // If no packet has made yet and maxSeqNb is equal to TxSeqNumber -1, then we can safely create a packet from connection buffer (sendingBuffer).
  if (p == 0 && ptrDSN == 0)
    {
      NS_ASSERT(!guard);
      NS_ASSERT(sFlow->maxSeqNb == sFlow->TxSeqNumber - 1);
      p = sendingBuffer.CreatePacket (size);
      if (p == 0)
        { // TODO I guess we should not return from here - What do we do then kill ourself?
          NS_LOG_WARN("["<< m_node->GetId() << "] ("<< sFlow->routeId << ") No data is available in SendingBuffer to create a pkt from it! SendingBufferSize: " << sendingBuffer.PendingData());
          NS_ASSERT_MSG(p != 0, "No data is available in SendingBuffer to create a pkt from it!");
          return 0;
        }
    }
  NS_ASSERT(packetSize <= size);
  NS_ASSERT(packetSize == p->GetSize ());

  // @SendDataPacket -> Add ECT Tag on every data packet... We do this for ack packets as well
  if (m_isDCTCPEnabled ||  m_ecn)
    AddPacketTag(p, ECT_TAG); //AddEctTag(p);
  // @SendDataPacket
  if (m_disjoinPath && flowType.compare ("Large") == 0)
    AddEcmpTag(p, sFlow->routeId);

  // This is data packet, so its TCP_Flag should be 0
  uint8_t flags = withAck ? TcpHeader::ACK : 0;

 
  TcpHeader header;
  header.SetFlags (flags);
  header.SetSequenceNumber (SequenceNumber32 (sFlow->TxSeqNumber));
  header.SetAckNumber (SequenceNumber32 (sFlow->RxSeqNumber));
  header.SetSourcePort (sFlow->sPort);
  header.SetDestinationPort (sFlow->dPort);
  header.SetWindowSize (AdvertisedWindowSize ());
  if (!guard)
    { // If packet is made from sendingBuffer, then we got to add the packet and its info to subflow's mapDSN.
      sFlow->AddDSNMapping (sFlowIdx, nextTxSequence, packetSize, sFlow->TxSeqNumber, sFlow->RxSeqNumber/*, p->Copy()*/);
    }
  if (!guard)
    { // if packet is made from sendingBuffer, then we use nextTxSequence to OptDSN
      header.AddOptDSN (OPT_DSN, nextTxSequence, packetSize, sFlow->TxSeqNumber);
    }
  else
    { // if packet is made from subflow's Buffer (already sent packets), that packet's dataSeqNumber should be added here!
      header.AddOptDSN (OPT_DSN, ptrDSN->dataSeqNumber, (uint16_t) packetSize, sFlow->TxSeqNumber);
      NS_ASSERT(packetSize == ptrDSN->dataLevelLength);
    }

  uint8_t hlen = 5;   // 5 --> 32-bit words = 20 Bytes == TcpHeader Size with out any option
  //uint8_t olen = 15;  // 15 because packet size is 2 bytes in size. 1 + 8 + 2+ 4 = 15
  uint8_t olen = 20;
  uint8_t plen = 0;
  plen = (4 - (olen % 4)) % 4; // (4 - (15 % 4)) 4 => 1
  olen = (olen + plen) / 4;    // (15 + 1) / 4 = 4
  hlen += olen;
  header.SetLength (hlen);
  header.SetOptionsLength (olen);
  header.SetPaddingLength (plen);

  NS_LOG_ERROR("hLen: " << (int)hlen << " oLen: " << (int)olen << " pLen: " << (int)plen);

  // Check RTO, if expired then reschedule it again.
  SetReTxTimeout (sFlowIdx);
  NS_LOG_LOGIC ("Send packet via TcpL4Protocol with flags 0x" << std::hex << static_cast<uint32_t> (flags) << std::dec);

  bool carryOn = true;

#ifdef MANUAL_DROP
  carryOn = ManualPacketDrop(sFlowIdx, p, packetSize);
#endif

  if (carryOn)
    {
      Ptr<NetDevice> netDevice = FindOutputNetDevice (sFlow->sAddr);
      m_tcp->SendPacket (p, header, sFlow->sAddr, sFlow->dAddr, netDevice);
    }
  if (!guard)
    sFlow->PktCount++;

#ifdef PLOT
  uint32_t tmp = (((sFlow->TxSeqNumber + packetSize) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->DATA.push_back (make_pair (Simulator::Now ().GetSeconds (), tmp));
#endif

  NS_LOG_LOGIC(Simulator::Now().GetSeconds() << " ["<< m_node->GetId()<< "] SendDataPacket->  " << header <<" dSize: " << packetSize<< " sFlow: " << sFlow->routeId);

  // Do some updates.....
  sFlow->rtt->SentSeq (SequenceNumber32 (sFlow->TxSeqNumber), packetSize); // Notify the RTT of a data packet sent
  sFlow->TxSeqNumber += packetSize; // Update subflow's nextSeqNum to send.
  sFlow->maxSeqNb = std::max (sFlow->maxSeqNb, sFlow->TxSeqNumber - 1);
  sFlow->m_highTxMark = std::max (sFlow->m_highTxMark, sFlow->TxSeqNumber - 1);
  if (!guard)
    {
      nextTxSequence += packetSize;  // Update connection sequence number
      //TxBytes += packetSize + 20 + 20 + 20 + 2;
    }
  //NS_LOG_UNCOND( "("<< (int) sFlowIdx<< ") DataPacket -----> " << header << "  " << m_localAddress << ":" << m_localPort<< "->" << m_remoteAddress << ":" << m_remotePort);

  // After data packet has been sent now look at remianing data in sending buffer
  uint32_t remainingData = sendingBuffer.PendingData ();
  if (m_closeOnEmpty && (remainingData == 0))
    {
      SendAllSubflowsFIN ();
//      flags |= TcpHeader::FIN; // Add FIN to the flag
//      if (sFlow->state == ESTABLISHED)
//        { // On active close: I am the first one to send FIN
//          NS_LOG_INFO ("(" << (int)sFlow->routeId<< ") ESTABLISHED -> FIN_WAIT_1 {SendPendingData} -> FIN is peggyback to last data Packet! MapDSN: " << sFlow->mapDSN.size() << ", pSize: " << p->GetSize());
//          sFlow->state = FIN_WAIT_1;
//        }
    }

  if (m_ratePlotCl || m_ratePlotSf || m_largePlotting)
    {
      m_totalSentBytes += packetSize;
      sFlow->totalSentByte += packetSize;
    }

  if (guard)
    return 0;
  else
    return packetSize;
}

#ifdef MANUAL_DROP
bool
MpTcpSocketBase::ManualPacketDrop (uint8_t sFlowIdx, Ptr<Packet> p, uint32_t packetSize)
{
  if (!hasSampleListDone)
    {
      sampleList.push_back(72);
      hasSampleListDone = true;
    }

  if (sampleList.size() > 0)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
      uint32_t pktNumber = (((sFlow->TxSeqNumber + packetSize) - sFlow->initialSequnceNumber) / sFlow->MSS);
      if (std::find (sampleList.begin (), sampleList.end (), pktNumber) != sampleList.end ())
        {
          uint32_t tmp = (((sFlow->TxSeqNumber + packetSize) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
          sFlow->DROP.push_back (make_pair (Simulator::Now ().GetSeconds (), tmp));
          sampleList.remove (pktNumber);
          return false;
        }
  }
  return true;
}
#endif

bool
MpTcpSocketBase::SendAllSubflowsFIN (void)
{
  NS_LOG_FUNCTION_NOARGS();
  // TODO FIN should be sent to all subflows regardless of their state DoClose() should deal with them properly....!
  /*  Scenario sent RST invalidate entire MPTCP connection and when subflow state is SYN_SENT DoClose() send RST on it.
   *  We don't want this happens for now as it might be a case that only initial subflow does the data delivery and rest of the subflows failed to established and remain in SYN_SENT!
   *  In current implementation it is expected that unestablished subflow being closed by CloseandNotify() at SendEmptyPacket().
   */
  if (sendingBuffer.PendingData () == 0)
    {
      //Ptr<UniformRandomVariable> uniRand = CreateObject<UniformRandomVariable>();
      vector<uint32_t> randomStorage;
      uint32_t randomGap = 0;
      uint32_t I = 0;
      vector<Ptr<MpTcpSubFlow> >::iterator SubflowCI;
      for (SubflowCI = subflows.begin (); SubflowCI != subflows.end (); ++SubflowCI)
        {
          Ptr<MpTcpSubFlow> sFlow = *SubflowCI;
          if (sFlow->state == ESTABLISHED)
            {
              NS_LOG_DEBUG(this << " " << Simulator::Now().GetSeconds() << " FIN has been sent to this subflow: " << sFlow->routeId << " nextTxSeqNumber: " << nextTxSequence << " CurrentSubflow: " << (int)currentSublow);
              if (I == 0)
                DoClose (sFlow->routeId);
              else
                {
                  do
                    {
                      //randomGap = uniRand->GetInteger(0, 50);
                      randomGap = rand () % 50;
                    }
                  while (std::find (randomStorage.begin (), randomStorage.end (), randomGap) != randomStorage.end ());
                  randomStorage.push_back (randomGap);
                  NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << sFlow->routeId <<") scheduled to send FIN for " << randomGap << "us");
                  Simulator::Schedule (MicroSeconds (randomGap), &MpTcpSocketBase::DoClose, this, sFlow->routeId);
                }
              I++;
            }
          else
            {
              NS_LOG_WARN(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] FIN did not sent to subflow(" << sFlow->routeId << ") since its state is " << TcpStateName[sFlow->state]);
            }
        }
    }
  return true;
}

void
MpTcpSocketBase::DoRetransmit (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // Retransmit SYN packet
  if (sFlow->state == SYN_SENT)
    {
//      if (sFlow->cnCount > 0)
//        {
//          SendEmptyPacket(sFlowIdx, TcpHeader::SYN);
//        }
//      else
//        {
//          NotifyConnectionFailed();
//        }
      NS_FATAL_ERROR("SYN has lost - We need to implement this :-)");
      return;
    }

  // Retransmit non-data packet: Only if in FIN_WAIT_1 or CLOSING state
  if (sendingBuffer.Empty () && sFlow->mapDSN.size () == 0)
    {
      if (sFlow->state == FIN_WAIT_1 || sFlow->state == CLOSING)
        { // Must have lost FIN, re-send
          NS_LOG_UNCOND("DoRetransmit -> Resent FIN... TxSeqNumber: " << sFlow->TxSeqNumber);
          SendEmptyPacket (sFlowIdx, TcpHeader::FIN);
        }
      return;
    }

  DSNMapping* ptrDSN = sFlow->GetunAckPkt ();
  if (ptrDSN == 0)
    {
      NS_LOG_INFO ("Retransmit -> no Unacked data !! mapDSN size is "<< sFlow->mapDSN.size() << " max Ack seq n?????? "<< sFlow->highestAck << " (" << (int)sFlowIdx<< ")");
      NS_ASSERT(3 != 3);
      return;
    }

  NS_ASSERT(ptrDSN->subflowSeqNumber == sFlow->highestAck + 1);

  // we retransmit only one lost pkt
  //Ptr<Packet> pkt = Create<Packet>(ptrDSN->packet, ptrDSN->dataLevelLength);
  Ptr<Packet> pkt = Create<Packet> (ptrDSN->dataLevelLength);
  TcpHeader header;
  header.SetSourcePort (sFlow->sPort);
  header.SetDestinationPort (sFlow->dPort);
  header.SetFlags (TcpHeader::NONE);  // Change to NONE Flag
  header.SetSequenceNumber (SequenceNumber32 (ptrDSN->subflowSeqNumber));
  header.SetAckNumber (SequenceNumber32 (sFlow->RxSeqNumber));  // for the acknowledgment, we ACK the sFlow last received data
  header.SetWindowSize (AdvertisedWindowSize ());

  header.AddOptDSN (OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber);

  uint8_t hlen = 5;
  uint8_t olen = 20; //uint8_t olen = 15;
  uint8_t plen = 0;
  plen = (4 - (olen % 4)) % 4;
  olen = (olen + plen) / 4;
  hlen += olen;
  header.SetLength (hlen);
  header.SetOptionsLength (olen);
  header.SetPaddingLength (plen);

  // @DoRetransmit -> Add ECT Tag on every data packet... We do this for ack packets as well
  if (m_isDCTCPEnabled ||  m_ecn)
    AddPacketTag(pkt, ECT_TAG);
  // @DoRetransmit
  if (m_disjoinPath && flowType.compare ("Large") == 0)
    AddEcmpTag (pkt, sFlow->routeId);

  m_tcp->SendPacket (pkt, header, sFlow->sAddr, sFlow->dAddr, FindOutputNetDevice (sFlow->sAddr));

  //reset RTO
  SetReTxTimeout (sFlowIdx);

#ifdef PLOT
  uint32_t tmp = (((ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->RETRANSMIT.push_back (make_pair (Simulator::Now ().GetSeconds (), tmp));
  if (!sFlow->m_inFastRec)
    {
      timeOutTrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->cwnd));
    }
#endif

  //TxBytes += ptrDSN->dataLevelLength + 62;

  // Update Rtt
  sFlow->rtt->SentSeq (SequenceNumber32 (ptrDSN->subflowSeqNumber), ptrDSN->dataLevelLength);

  // In case of RTO, advance m_nextTxSequence
  sFlow->TxSeqNumber = std::max (sFlow->TxSeqNumber, ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength);
  sFlow->maxSeqNb = std::max (sFlow->maxSeqNb, sFlow->TxSeqNumber - 1);
  sFlow->m_highTxMark = std::max (sFlow->m_highTxMark, sFlow->TxSeqNumber - 1);
}

void
MpTcpSocketBase::DoRetransmit (uint8_t sFlowIdx, DSNMapping* ptrDSN)
{
  NS_LOG_FUNCTION(this);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // This retransmit segment should be the lost segment.
  NS_ASSERT(ptrDSN->subflowSeqNumber >= sFlow->highestAck + 1);

  SetReTxTimeout (sFlowIdx); // reset RTO

  // we retransmit only one lost pkt
  //Ptr<Packet> pkt = Create<Packet>(ptrDSN->packet, ptrDSN->dataLevelLength);
  Ptr<Packet> pkt = Create<Packet> (ptrDSN->dataLevelLength);
  if (pkt == 0)
    NS_ASSERT(3 != 3);

  TcpHeader header;
  header.SetSourcePort (sFlow->sPort);
  header.SetDestinationPort (sFlow->dPort);
  header.SetFlags (TcpHeader::NONE);  // Change to NONE Flag
  header.SetSequenceNumber (SequenceNumber32 (ptrDSN->subflowSeqNumber));
  header.SetAckNumber (SequenceNumber32 (sFlow->RxSeqNumber));
  header.SetWindowSize (AdvertisedWindowSize ());
  // Make sure info here comes from ptrDSN...
  header.AddOptDSN (OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber);

  NS_LOG_WARN (Simulator::Now().GetSeconds() <<" RetransmitSegment -> "<< " localToken "<< localToken<<" Subflow "<<(int) sFlowIdx<<" DataSeq "<< ptrDSN->dataSeqNumber <<" SubflowSeq " << ptrDSN->subflowSeqNumber <<" dataLength " << ptrDSN->dataLevelLength << " packet size " << pkt->GetSize() << " 3DupACK");
  uint8_t hlen = 5;
  uint8_t olen = 20; //uint8_t olen = 15;
  uint8_t plen = 0;
  plen = (4 - (olen % 4)) % 4;
  olen = (olen + plen) / 4;
  hlen += olen;
  header.SetLength (hlen);
  header.SetOptionsLength (olen);
  header.SetPaddingLength (plen);

  // @DoRetransmit -> Add ECT Tag on every data packet... We do this for ack packets as well
  if (m_isDCTCPEnabled ||  m_ecn)
    AddPacketTag (pkt, ECT_TAG);
  // @DoRetransmit
  if (m_disjoinPath && flowType.compare ("Large") == 0)
    AddEcmpTag(pkt, sFlow->routeId);

  // Send Segment to lower layer
  m_tcp->SendPacket (pkt, header, sFlow->sAddr, sFlow->dAddr, FindOutputNetDevice (sFlow->sAddr));
#ifdef PLOT
  uint32_t tmp = (((ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->RETRANSMIT.push_back (make_pair (Simulator::Now ().GetSeconds (), tmp));
#endif

  //TxBytes += ptrDSN->dataLevelLength + 62;

  // Notify RTT
  sFlow->rtt->SentSeq (SequenceNumber32 (ptrDSN->subflowSeqNumber), ptrDSN->dataLevelLength);

  // In case of RTO, advance m_nextTxSequence
  sFlow->TxSeqNumber = std::max (sFlow->TxSeqNumber, ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength);

  // highest sent sequence number should be updated!
  sFlow->maxSeqNb = std::max (sFlow->maxSeqNb, sFlow->TxSeqNumber - 1);
  sFlow->m_highTxMark = std::max (sFlow->m_highTxMark, sFlow->TxSeqNumber - 1);

  NS_LOG_INFO("("<<(int) sFlowIdx << ") DoRetransmit -> " << header);
}
void
MpTcpSocketBase::DiscardUpTo (uint8_t sFlowIdx, uint32_t ack)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  list<DSNMapping *>::iterator current = sFlow->mapDSN.begin ();
  list<DSNMapping *>::iterator next = sFlow->mapDSN.begin ();
  while (current != sFlow->mapDSN.end ())
    {
      ++next;
      DSNMapping *ptrDSN = *current;
      // All segments before ackSeqNum should be removed from the mapDSN list. Maybe equal part never run due to if condition above.
      if (ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength <= ack)
        {
          next = sFlow->mapDSN.erase (current);
          delete ptrDSN;
        }
      current = next;
    }
}

// .....................................................................................................
uint8_t
MpTcpSocketBase::GetMaxSubFlowNumber ()
{
  return maxSubflows;
}

void
MpTcpSocketBase::SetMaxSubFlowNumber (uint8_t num)
{
  maxSubflows = num;
}
//...........................................................................................
// Following implementation has derived from tcp-reno implementation
//...........................................................................................
void
MpTcpSocketBase::SetSSThresh (uint32_t threshold)
{
  m_ssThresh = threshold;
}

uint32_t
MpTcpSocketBase::GetSSThresh (void) const
{
  return m_ssThresh;
}

void
MpTcpSocketBase::SetInitialCwnd (uint32_t cwnd)
{
  NS_ABORT_MSG_UNLESS(m_state == CLOSED,
                      "MpTcpsocketBase::SetInitialCwnd() cannot change initial cwnd after connection started.");
  m_initialCWnd = cwnd;
}

uint32_t
MpTcpSocketBase::GetInitialCwnd (void) const
{
  return m_initialCWnd;
}

Ptr<TcpSocketBase>
MpTcpSocketBase::Fork (void)
{
  NS_LOG_FUNCTION_NOARGS();
  return CopyObject<MpTcpSocketBase> (this);
}

/** Cut cwnd and enter fast recovery mode upon triple dupack */
void
MpTcpSocketBase::DupAck (const TcpHeader& t, uint32_t count)
{
  NS_LOG_FUNCTION_NOARGS();
}
//...........................................................................................

int
MpTcpSocketBase::Listen (void)
{
  NS_LOG_FUNCTION(this);

  if (m_state != CLOSED)
    {
      m_errno = ERROR_INVAL;
      return -1;
    }

  // MPTCP connection state is LISTEN
  m_state = LISTEN;
  return 0;
}

int
MpTcpSocketBase::Connect (Ipv4Address servAddr, uint16_t servPort)
{
  NS_LOG_FUNCTION(this << servAddr << servPort);  //
  Ptr<MpTcpSubFlow> sFlow = CreateObject<MpTcpSubFlow> ();
  sFlow->routeId = (subflows.size () == 0 ? 0 : subflows[subflows.size () - 1]->routeId + 1);
  sFlow->dAddr = servAddr;    // Assigned subflow destination address
  sFlow->dPort = servPort;    // Assigned subflow destination port
  m_remoteAddress = servAddr; // MPTCP Connection's remote address
  m_remotePort = servPort;    // MPTCP Connection's remote port

  if (m_endPoint == 0)
    {
      if (Bind () == -1) // Bind(), if there is no endpoint for this socket
        {
          NS_ASSERT(m_endPoint == 0);
          return -1; // Bind() failed.
        }
      // Make sure endpoint is created.
      NS_ASSERT(m_endPoint != 0);
    }
  // Set up remote addr:port for this endpoint as we knew it from Connect's parameters
  m_endPoint->SetPeer (servAddr, servPort);

  if (m_endPoint->GetLocalAddress () == "0.0.0.0")
    {
      // Find approapriate local address from the routing protocol for this endpoint.
      if (SetupEndpoint () != 0)
        { // Route to destination does not exist.
          return -1;
        }
    }
  else
    { // Make sure there is an route from source to destination. Source might be set wrongly.
      if ((IsThereRoute (m_endPoint->GetLocalAddress (), servAddr)) == false)
        {
          NS_LOG_INFO("Connect -> There is no route from " << m_endPoint->GetLocalAddress() << " to " << m_endPoint->GetPeerAddress());
          //m_tcp->DeAllocate(m_endPoint); // this would fire up destroy function...
          return -1;
        }
    }

  // Set up subflow local addrs:port from endpoint
  sFlow->sAddr = m_endPoint->GetLocalAddress ();
  sFlow->sPort = m_endPoint->GetLocalPort ();
  sFlow->MSS = segmentSize;
  sFlow->cwnd = sFlow->MSS;
  NS_LOG_UNCOND ("Connect -> SegmentSize: " << sFlow->MSS << " tcpSegmentSize: " << m_segmentSize << " segmentSize: " << segmentSize << "SendingBufferSize: " << sendingBuffer.bufMaxSize);

  // This is master subsocket (master subflow) then its endpoint is the same as connection endpoint.
  sFlow->m_endPoint = m_endPoint;
  subflows.insert (subflows.end (), sFlow);
//  m_tcp->m_sockets.push_back(this); //TMP REMOVE

  sFlow->rtt->Reset (); // Dangerous ?!?!?! Not really?
  sFlow->rtt->SetG (m_g); // DCTCP
  sFlow->cnTimeout = m_cnTimeout;
  sFlow->cnRetries = m_cnRetries;
  sFlow->cnCount = sFlow->cnRetries;

//  if (sFlow->state == CLOSED || sFlow->state == LISTEN || sFlow->state == SYN_SENT || sFlow->state == LAST_ACK || sFlow->state == CLOSE_WAIT)
//    { // send a SYN packet and change state into SYN_SENT
  NS_LOG_INFO ("("<< (int)sFlow->routeId << ") "<< TcpStateName[sFlow->state] << " -> SYN_SENT");
  m_state = SYN_SENT;
  sFlow->state = SYN_SENT;  // Subflow state should be change first then SendEmptyPacket...
  SendEmptyPacket (sFlow->routeId, TcpHeader::SYN);
  currentSublow = sFlow->routeId; // update currentSubflow in case close just after 3WHS.
  NS_LOG_INFO(this << "  MPTCP connection is initiated (Sender): " << sFlow->sAddr << ":" << sFlow->sPort << " -> " << sFlow->dAddr << ":" << sFlow->dPort << " m_state: " << TcpStateName[m_state]);

  return 0;
}

int
MpTcpSocketBase::Connect (const Address &address)
{
  NS_LOG_FUNCTION ( this << address );
  InetSocketAddress transport = InetSocketAddress::ConvertFrom (address);
  m_remoteAddress = transport.GetIpv4 (); // MPTCP Connection remoteAddress
  m_remotePort = transport.GetPort (); // MPTCP Connection remotePort
  return Connect (m_remoteAddress, m_remotePort);
}

/** Inhereted from Socket class: Bind socket to an end-point in MpTcpL4Protocol */
int
MpTcpSocketBase::Bind ()
{
  NS_LOG_FUNCTION (this);
  client = true;
  m_endPoint = m_tcp->Allocate ();  // Create endPoint with ephemeralPort.
  if (0 == m_endPoint)
    {
      m_errno = ERROR_ADDRNOTAVAIL;
      return -1;
    }
  //m_tcp->m_sockets.push_back(this); // We don't need it for now
  return SetupCallback ();
}

/** Clean up after Bind operation. Set up callback function in the end-point */
int
MpTcpSocketBase::SetupCallback ()
{
  NS_LOG_FUNCTION(this);
  if (m_endPoint == 0)
    {
      return -1;
    }
  // set the call backs method
  m_endPoint->SetRxCallback (MakeCallback (&MpTcpSocketBase::ForwardUp, Ptr<MpTcpSocketBase> (this)));
  m_endPoint->SetDestroyCallback (MakeCallback (&MpTcpSocketBase::Destroy, Ptr<MpTcpSocketBase> (this)));

  // Setup local add:port of this mptcp endpoint.
  m_localAddress = m_endPoint->GetLocalAddress ();
  m_localPort = m_endPoint->GetLocalPort ();

  return 0;
}

/** Inherit from socket class: Bind socket (with specific address) to an end-point in TcpL4Protocol */
int
MpTcpSocketBase::Bind (const Address &address)
{
  NS_LOG_FUNCTION (this<<address);
  server = true;
  if (!InetSocketAddress::IsMatchingType (address))
    {
      m_errno = ERROR_INVAL;
      return -1;
    }

  InetSocketAddress transport = InetSocketAddress::ConvertFrom (address);
  Ipv4Address ipv4 = transport.GetIpv4 ();
  uint16_t port = transport.GetPort ();

  if (ipv4 == Ipv4Address::GetAny () && port == 0)
    {
      m_endPoint = m_tcp->Allocate ();
    }
  else if (ipv4 == Ipv4Address::GetAny () && port != 0)
    { // Allocate with specific port
      m_endPoint = m_tcp->Allocate (port);
    }
  else if (ipv4 != Ipv4Address::GetAny () && port == 0)
    { // Allocate with specific ipv4 address
      m_endPoint = m_tcp->Allocate (ipv4);
    }
  else if (ipv4 != Ipv4Address::GetAny () && port != 0)
    { // Allocate with specific Ipv4 add:port
      m_endPoint = m_tcp->Allocate (ipv4, port);
    }
  else
    {
      NS_LOG_ERROR("Bind to specific add:port has failed!");
    }

  //m_tcp->m_sockets.push_back(this); // we don't need it for now
  NS_LOG_LOGIC("MpTcpSocketBase:Bind(addr) " << this << " got an endpoint " << m_endPoint << " localAddr " << m_endPoint->GetLocalAddress() << ":" << m_endPoint->GetLocalPort() << " RemoteAddr " << m_endPoint->GetPeerAddress() << ":"<< m_endPoint->GetPeerPort());
  return SetupCallback ();
}

bool
MpTcpSocketBase::SendBufferedData ()
{
  return SendPendingData ();
}

//int
//MpTcpSocketBase::FillBuffer(uint8_t* buf, uint32_t size)
//{
//  NS_LOG_FUNCTION( this << size );
//  return sendingBuffer->Add(buf, size);
//}

int
MpTcpSocketBase::FillBuffer (uint32_t size)
{
  NS_LOG_FUNCTION( this << size );
  return sendingBuffer.Add (size);
}

/**
 * Sending data via subflows with available window size. It sends data only to ESTABLISHED subflows.
 * It sends data by calling SendDataPacket() function.
 * Called by functions: SendBufferedData(), ReceveidAck(routeId), NewAck(routeID) & DupAck(routeID), ProcessSynSent(routeId)
 */
bool
MpTcpSocketBase::SendPendingData (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION(this);

  // DCTCP: If we have received ECN echo in some of the received ACKs, halve the congestion window
  if (m_isDCTCPEnabled && sFlowIdx < maxSubflows && m_eceBit > 0 && subflows[sFlowIdx]->state == ESTABLISHED
      && subflows[sFlowIdx]->dctcp_maxseq < (subflows[sFlowIdx]->highestAck + 1))
    {
      NS_LOG_INFO ("Halving CWND because we've received ECN Echo.");
      NS_ASSERT(client);
      if (m_slowDownEcnLike)
        SlowDownEcnLike (sFlowIdx);
      else
        SlowDown (sFlowIdx);
    }

  // This condition only valid when sendingBuffer is empty!
  if (sendingBuffer.Empty () && sFlowIdx < maxSubflows)
    {
      uint32_t whileCounter = 0;
      Ptr<MpTcpSubFlow> sF = subflows[sFlowIdx];
      if (sF->mapDSN.size () > 0 && sF->maxSeqNb > sF->TxSeqNumber - 1)
        { // SendingBuffer is empty but subflowBuffer (mapDSN) is not. Also subflow is recovering from timeOut.
          uint32_t window = std::min (AvailableWindow (sFlowIdx), sF->MSS);
          // Send all data packets in subflowBuffer (mapDSN) until subflow's available window is full.
          while (window != 0 && window >= sF->MSS && sF->maxSeqNb > sF->TxSeqNumber - 1 && sF->mapDSN.size () > 0)
            { // In case case more than one packet can be sent, if subflow's window allow
              whileCounter++;
              NS_LOG_UNCOND("["<< m_node->GetId() <<"] MainBuffer is empty - subflowBuffer(" << sF->mapDSN.size()<< ") sFlow("<< (int)sFlowIdx << ") AvailableWindow: " << window << " CWND: " << sF->cwnd << " subflow is in timoutRecovery{" << (sF->mapDSN.size() > 0) << "} LoopIter: " << whileCounter);
              int ret = SendDataPacket (sF->routeId, window, false);
              if (ret < 0)
                {
                  NS_LOG_UNCOND(this <<" [" << m_node->GetId() << "]("<< sF->routeId << ")" << " SendDataPacket return -1 -> Return false from SendPendingData()!?");
                  return false; // Return -1 from SendDataPacket means segment match has not find from subflow buffer, so this loop should be stopped and return!!
                }
              NS_ASSERT(ret == 0);
              window = std::min (AvailableWindow (sFlowIdx), sF->MSS);
            }
          return false;  // SendingBuffer is empty so no point to continue further on this function
        }
      else
        { // SendingBuffer & subflowBuffer are empty i.e, nothing to re-send and nothing to send!!
          NS_LOG_LOGIC(Simulator::Now().GetSeconds()<< " [" << m_node->GetId() << "]" << " SendPendingData -> SubflowBuffer and main buffer is empty -> Return!");
          return false; // SendingBuffer is empty so no point to continue further on this function
        }
    }

  // No endPoint -> Can't send any data
  if (m_endPoint == 0)
    {
      NS_LOG_ERROR ("["<< m_node->GetId() <<"] MpTcpSocketBase::SendPendingData:-> No endpoint");
      NS_ASSERT_MSG(m_endPoint != 0, " No endpoint");
      return false; // Is this the right way to handle this condition?
    }

  uint32_t nOctetsSent = 0;
  Ptr<MpTcpSubFlow> sFlow;

  // Send data as much as possible (it depends on subflows AvailableWindow and data in sending buffer)
  while (!sendingBuffer.Empty ())
    {
      uint32_t window = 0;
      // Search for a subflow with available windows
      if (m_isAdaptiveSubflow &&  IncastDetected())
        { // When Incast is detected use initial subflow only
          assert (maxSubflows >= 2);
          lastUsedsFlowIdx = 0;
          window = std::min (AvailableWindow (lastUsedsFlowIdx), sendingBuffer.PendingData ()); // Get available window size
        }
      else
        { // Normal operation
          for (uint32_t i = 0; i < subflows.size (); i++)
            {
              if (subflows[lastUsedsFlowIdx]->state != ESTABLISHED)
                continue;
              window = std::min (AvailableWindow (lastUsedsFlowIdx), sendingBuffer.PendingData ()); // Get available window size
              if (window == 0)
                {  // No more available window in the current subflow, try with another one
                  NS_LOG_LOGIC("SendPendingData -> No window available on (" << (int)lastUsedsFlowIdx << ") Try next one!");
                  lastUsedsFlowIdx = getSubflowToUse ();
                }
              else
                {
                  NS_LOG_LOGIC ("SendPendingData -> Find subflow with spare window PendingData (" << sendingBuffer.PendingData() << ") Available window ("<<AvailableWindow (lastUsedsFlowIdx)<<")");
                  break;
                }
            }
        }

      if (window == 0)
        break;

      // Take a pointer to the subflow with available window.
      sFlow = subflows[lastUsedsFlowIdx];

      // By this condition only connection initiator can send data need to be change though!
      if (sFlow->state == ESTABLISHED)
        {
          currentSublow = sFlow->routeId;
          uint32_t s = std::min (window, sFlow->MSS);  // Send no more than window
          if (sFlow->maxSeqNb > sFlow->TxSeqNumber - 1 && sendingBuffer.PendingData () <= sFlow->MSS)
            { // When subflow is in timeout recovery and the last segment is not reached yet then segment size should be equal to MSS
              s = sFlow->MSS;
            }
          int amountSent = SendDataPacket (sFlow->routeId, s, false);
          if (amountSent < 0)
            {
              NS_LOG_UNCOND(this <<" [" << m_node->GetId() << "]("<< sFlow->routeId << ")" << " SendDataPacket return -1 -> Return false from SendPendingData()!?");
              return false;
            }
          else
            nOctetsSent += amountSent;  // Count total bytes sent in this loop
        } // end of if statement
      lastUsedsFlowIdx = getSubflowToUse ();
    } // end of main while loop
  //NS_LOG_UNCOND ("["<< m_node->GetId() << "] SendPendingData -> amount data sent = " << nOctetsSent << "... Notify application.");
  if (nOctetsSent > 0)
    NotifyDataSent (GetTxAvailable ());
  return (nOctetsSent > 0);
}

uint8_t
MpTcpSocketBase::getSubflowToUse ()
{
  NS_LOG_FUNCTION(this);
  uint8_t nextSubFlow = 0;
  switch (distribAlgo)
    {
  case Round_Robin:
    nextSubFlow = (lastUsedsFlowIdx + 1) % subflows.size ();
    break;
  default:
    break;
    }
  return nextSubFlow;
}

/**
 TCP: Upon RTO:
 1) ssthresh is set to half of flight size
 2) cwnd is set to 1*MSS
 3) retransmit the lost packet
 4) Tcp back to slow start
 */

void
MpTcpSocketBase::ReTxTimeout (uint8_t sFlowIdx)
{ // Retransmit timeout
  NS_LOG_FUNCTION (this);
  NS_ASSERT_MSG(client, "ReTxTimeout is not implemented for server side yet");
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  NS_LOG_INFO ("Subflow ("<<(int)sFlowIdx<<") ReTxTimeout Expired at time "<<Simulator::Now ().GetSeconds()<< " unacked packets count is "<<sFlow->mapDSN.size() << " sFlow->state: " << TcpStateName[sFlow->state]); //
  //NS_LOG_INFO("TxSeqNb: " << sFlow->TxSeqNumber << " HighestAck: " << sFlow->highestAck);
  // If erroneous timeout in closed/timed-wait state, just return
  if (sFlow->state == CLOSED || sFlow->state == TIME_WAIT)
    {
      NS_LOG_INFO("RETURN");
      //NS_ASSERT(3!=3);
      return;
    }
  // If all data are received (non-closing socket and nothing to send), just return
  // if (m_state <= ESTABLISHED && m_txBuffer.HeadSequence() >= m_highTxMark)
  if (sFlow->state <= ESTABLISHED && sFlow->mapDSN.size () == 0)
    {
      NS_LOG_INFO("ReTxTimeOut(" << (int)sFlowIdx << ") -> " << TcpStateName[sFlow->state]);
      //NS_ASSERT(3!=3); // DANGEROUS
      return;
    }
  Retransmit (sFlowIdx); // Retransmit the packet
}

void
MpTcpSocketBase::ReduceCWND (uint8_t sFlowIdx, DSNMapping* ptrDSN)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint32_t mss = sFlow->MSS;
  uint32_t flightSize = std::min(BytesInFlight (sFlowIdx), sFlow->cwnd.Get()); //To avoid we need to take the min of flight size and c_wnd
  int d = 0;
  calculateTotalCWND ();

  switch (AlgoCC)
    {
  case Uncoupled_TCPs:
  case Linked_Increases:
  case RTT_Compensator:
  case Fast_Uncoupled:
  case Fast_Increases:
  case COUPLED_INC:
  case COUPLED_EPSILON:
  case UNCOUPLED:
  case XCA:
  //sFlow->ssthresh = std::max (2 * mss, BytesInFlight (sFlowIdx) / 2);
    sFlow->ssthresh = std::max (2 * mss, flightSize/2);
    sFlow->cwnd = sFlow->ssthresh + 3 * mss;
    break;

  case COUPLED_SCALABLE_TCP:
    d = (int) sFlow->cwnd.Get () - (compute_total_window () >> 3);
    if (d < 0)
      d = 0;
    sFlow->ssthresh = max (2 * mss, (uint32_t) d);
    sFlow->cwnd = sFlow->ssthresh + 3 * mss;
    break;

  case COUPLED_FULLY:
    d = (int) sFlow->cwnd.Get () - compute_total_window () / B;
    if (d < 0)
      d = 0;
    sFlow->ssthresh = max (2 * mss, (uint32_t) d);
    sFlow->cwnd = sFlow->ssthresh + 3 * mss;
    break;

  case Fully_Coupled:
    d = sFlow->cwnd.Get () - totalCwnd / 2;
    if (d < 0)
      d = 0;
    sFlow->ssthresh = std::max (2 * mss, (uint32_t) d);
    sFlow->cwnd = sFlow->ssthresh + 3 * mss;
    break;

  default:
    NS_ASSERT(3 != 3);
    break;
    }
  // update
//  sFlow->m_recover = SequenceNumber32 (sFlow->maxSeqNb + 1);
  sFlow->m_recover = SequenceNumber32 (sFlow->m_highTxMark + 1);
  sFlow->m_inFastRec = true;
  //We have inflated the window by 3 segment sizes, record it
  sFlow->m_duplicatesSize = 3 * mss;
//  sFlow->m_ssThreshLastChange = Simulator::Now (); // DCTCP

  // Retrasnmit a specific packet (lost segment)
  DoRetransmit (sFlowIdx, ptrDSN);
#ifdef PLOT
  reTxTrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->cwnd));
  sFlow->ssthreshtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->ssthresh));
#endif
}

/** Retransmit timeout */
void
MpTcpSocketBase::Retransmit (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this);  //
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  if(!sFlow->m_inFastRec){
      sFlow->ssthresh = std::max (2 * sFlow->MSS, BytesInFlight (sFlowIdx) / 2);
    }
    else{
      //If already in fast recovery, halve ssThresh again
      sFlow->ssthresh = sFlow->ssthresh / 2;
      sFlow->m_duplicatesSize = 0;
    }
  sFlow->m_inFastRec = false;
//sFlow->m_ssThreshLastChange = Simulator::Now (); // DCTCP
  sFlow->cwnd = sFlow->MSS; //  sFlow->cwnd = 1.0;
  sFlow->TxSeqNumber = sFlow->highestAck + 1; // m_nextTxSequence = m_txBuffer.HeadSequence(); // Restart from highest Ack
  sFlow->m_highTxMark = sFlow->TxSeqNumber - 1; //m_highTxMark = m_nextTxSequence - m_segmentSize;

  //DCTCP update duing Timeout
  if (m_isDCTCPEnabled || m_dctcpFastReTxRecord)
    { // Determine the next observation window for updating dctcp's alpha
      sFlow->dctcp_alpha_update_seq = sFlow->TxSeqNumber;
      sFlow->dctcp_maxseq = sFlow->dctcp_alpha_update_seq;
    }

  //if (!(sendingBuffer->Empty() && sFlow->mapDSN.size() > 0))
  sFlow->rtt->IncreaseMultiplier ();  // Double the next RTO

  if (AlgoCC >= COUPLED_EPSILON && AlgoCC < Fast_Uncoupled)
    window_changed ();

  DoRetransmit (sFlowIdx);  // Retransmit the packet
#ifdef PLOT
  sFlow->_TimeOut.push_back (make_pair (Simulator::Now ().GetSeconds (), TimeScale));
#endif
  TimeOuts++;
  // rfc 3782 - Recovering from timeOut
  //sFlow->m_recover = SequenceNumber32(sFlow->maxSeqNb + 1);
}

void
MpTcpSocketBase::SetReTxTimeout (uint8_t sFlowIdx)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  if (sFlow->retxEvent.IsExpired ())
    {
      Time rto = sFlow->rtt->RetransmitTimeout ();
      sFlow->retxEvent = Simulator::Schedule (rto, &MpTcpSocketBase::ReTxTimeout, this, sFlowIdx);
    }
}

DSNMapping*
MpTcpSocketBase::getAckedSegment (uint8_t sFlowIdx, uint32_t ack)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  DSNMapping* ptrDSN = 0;
  for (list<DSNMapping *>::iterator it = sFlow->mapDSN.begin (); it != sFlow->mapDSN.end (); ++it)
    {
      DSNMapping* dsn = *it;
      if (dsn->subflowSeqNumber + dsn->dataLevelLength == ack)
        {
          ptrDSN = dsn;
          break;
        }
    }
  return ptrDSN;
}

DSNMapping*
MpTcpSocketBase::getSegmentOfACK (uint8_t sFlowIdx, uint32_t ack)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  DSNMapping* ptrDSN = 0;
  for (list<DSNMapping *>::iterator it = sFlow->mapDSN.begin (); it != sFlow->mapDSN.end (); ++it)
    {
      DSNMapping* dsn = *it;
      if (dsn->subflowSeqNumber == ack)
        {
          ptrDSN = dsn;
          break;
        }
    }
  return ptrDSN;
}
void
MpTcpSocketBase::NewAckNewReno (uint8_t sFlowIdx, const TcpHeader& mptcpHeader, TcpOptions* opt)
{
  NS_LOG_FUNCTION (this << (int)sFlowIdx);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  SequenceNumber32 ack = mptcpHeader.GetAckNumber ();
  uint32_t ackedBytes = ack.GetValue () - (sFlow->highestAck + 1);

  NS_LOG_LOGIC ("TcpNewReno receieved ACK for seq " << ack <<" cwnd " << sFlow->cwnd <<" ssthresh " << sFlow->ssthresh);
  // Check for exit condition of fast recovery
  if (sFlow->m_inFastRec && ack < sFlow->m_recover)
    { // Partial ACK, partial window deflation (RFC2582 sec.3 bullet #5 paragraph 3)
      NS_LOG_WARN("NewAckNewReno -> ");
//      sFlow->cwnd -= ack.GetValue () - (sFlow->highestAck + 1); // data bytes where acked
      uint32_t diff = ack.GetValue () - (sFlow->highestAck + 1);
      if (diff > sFlow->m_duplicatesSize)
        {
          diff = sFlow->m_duplicatesSize;
        }
      sFlow->m_duplicatesSize -= diff;
      NS_ASSERT(diff <= sFlow->cwnd); //Don't let the cWnd wrap-around
      sFlow->cwnd -= diff; //The actual deflation
      sFlow->cwnd += m_segmentSize;  // increase cwnd
      sFlow->m_duplicatesSize += m_segmentSize;
      // RFC3782 sec.5, partialAck condition for inflating.
//      sFlow->cwnd += sFlow->MSS; // increase cwnd
#ifdef PLOT
      NS_LOG_LOGIC ("Partial ACK in fast recovery: cwnd set to " << sFlow->cwnd.Get());
      PartialAck.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->cwnd.Get ()));
      sFlow->ssthreshtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->ssthresh));
      sFlow->_FR_PA.push_back (make_pair (Simulator::Now ().GetSeconds (), TimeScale));
#endif
      DiscardUpTo (sFlowIdx, ack.GetValue ());
      DSNMapping* ptrDSN = getSegmentOfACK (sFlowIdx, ack.GetValue ());
      NS_ASSERT(ptrDSN != 0);
      DoRetransmit (sFlowIdx, ptrDSN);

      NewACK (sFlowIdx, mptcpHeader, opt); // update m_nextTxSequence and send new data if allowed by window
      //DoRetransmit(sFlowIdx); // Assume the next seq is lost. Retransmit lost packet
      pAck++;
      return;
    }
  else if (sFlow->m_inFastRec && ack >= sFlow->m_recover)
    { // Full ACK (RFC2582 sec.3 bullet #5 paragraph 2, option 1)
      //NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() <<"] (" << (int)sFlowIdx << ") NewAckNewReno -> FullAck");
      sFlow->cwnd = std::min (sFlow->ssthresh, BytesInFlight (sFlowIdx) + sFlow->MSS);
      sFlow->m_duplicatesSize= 0; //Reset the duplicate size since we're leaving fast recovery
      // Exit from Fast recovery
      sFlow->m_inFastRec = false;
      FullAcks++;
#ifdef PLOT
      FullAck.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->cwnd.Get ()));
      sFlow->ssthreshtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->ssthresh));
      sFlow->_FR_FA.push_back (make_pair (Simulator::Now ().GetSeconds (), TimeScale));
#endif
    }

  if (!(sFlow->mapDSN.size () == 0 && sendingBuffer.Empty () && sFlow->state == FIN_WAIT_1))
    { 
        OpenCWND (sFlowIdx, ackedBytes);
    }

  // Complete newAck processing
  NewACK (sFlowIdx, mptcpHeader, opt);      // update m_nextTxSequence and send new data if allowed by window
}

void
MpTcpSocketBase::NewACK (uint8_t sFlowIdx, const TcpHeader& mptcpHeader, TcpOptions* opt)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint32_t ack = (mptcpHeader.GetAckNumber ()).GetValue ();
  NS_LOG_LOGIC ("[" << m_node->GetId()<< "]" << " Cancelled ReTxTimeout event which was set to expire at " << (Simulator::Now () + Simulator::GetDelayLeft (sFlow->retxEvent)).GetSeconds ());

  // On recieving a "New" ack we restart retransmission timer .. RFC 2988
  sFlow->retxEvent.Cancel ();
  Time rto = sFlow->rtt->RetransmitTimeout ();
  NS_LOG_LOGIC (this << " Schedule ReTxTimeout at time> " <<Simulator::Now ().GetSeconds () << " to expire at time " <<(Simulator::Now () + rto).GetSeconds ());
  sFlow->retxEvent = Simulator::Schedule (rto, &MpTcpSocketBase::ReTxTimeout, this, sFlowIdx);

  // Note the highest ACK and tell app to send more
  DiscardUpTo (sFlowIdx, ack);

  if (GetTxAvailable () > 0)
    { // Notify app about free space available in TxBuffer
      NotifyDataSent (GetTxAvailable ()); // NotifySend(GetTxAvailable());
    }
  if (ack > sFlow->TxSeqNumber)
    {
      NS_LOG_WARN("NewAck-> ReceivedAck is bigger than TxSeqNumber => Advance TxSeqNumber from " << sFlow->TxSeqNumber << " To " << ack);
      sFlow->TxSeqNumber = ack; // If advanced
    }

  if (sendingBuffer.Empty () && sFlow->mapDSN.size () == 0 && sFlow->state != FIN_WAIT_1 && sFlow->state != CLOSING)
    { // No retransmit timer if no data to retransmit
      NS_LOG_INFO ("("<< (int)sFlow->routeId << ") NewAck -> Cancelled ReTxTimeout event which was set to expire at " << (Simulator::Now () + Simulator::GetDelayLeft (sFlow->retxEvent)).GetSeconds () << ", DSNmap: " << sFlow->mapDSN.size());
      sFlow->retxEvent.Cancel ();
    }

  sFlow->highestAck = std::max (sFlow->highestAck, ack - 1);
  NS_LOG_WARN("NewACK-> sFlow->highestAck: " << sFlow->highestAck);

  currentSublow = sFlow->routeId;
  SendPendingData (sFlow->routeId); // in newack()
}

void
MpTcpSocketBase::SendEmptyPacket (uint8_t sFlowIdx, uint8_t flags)
{
  NS_LOG_FUNCTION (this << (int)sFlowIdx);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  Ptr<Packet> p = Create<Packet> ();

  SequenceNumber32 s = SequenceNumber32 (sFlow->TxSeqNumber);

  if (sFlow->m_endPoint == 0)
    {
      NS_FATAL_ERROR("Failed to send empty packet due to null subflow's endpoint"); NS_LOG_WARN ("Failed to send empty packet due to null subflow's endpoint");
      return;
    }

  if (flags & TcpHeader::FIN)
    {
      //flags |= TcpHeader::ACK;
      if (sFlow->maxSeqNb != sFlow->TxSeqNumber - 1)
        {
          NS_ASSERT(client);
          s = sFlow->maxSeqNb + 1;
        }
    }
  else if (m_state == FIN_WAIT_1 || m_state == LAST_ACK || m_state == CLOSING)
    {
      ++s;
    }

  TcpHeader header;
  uint8_t hlen = 0;
  uint8_t olen = 0;

  header.SetSourcePort (sFlow->sPort);
  header.SetDestinationPort (sFlow->dPort);
  header.SetFlags (flags);
  header.SetSequenceNumber (s);
  header.SetAckNumber (SequenceNumber32 (sFlow->RxSeqNumber));
  header.SetWindowSize (AdvertisedWindowSize ());

  bool hasSyn = flags & TcpHeader::SYN;
  bool hasFin = flags & TcpHeader::FIN;
  bool isAck = flags == TcpHeader::ACK;

  Time RTO = sFlow->rtt->RetransmitTimeout ();
  if (hasSyn)
    {
      if (sFlow->cnCount == 0)
        { // No more connection retries, give up
          cout << "[" << m_node->GetId () << "]{" << flowId << "}(" << flowType << ")" << sFlow->cnCount << endl;
          NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " ["<< m_node->GetId() << "] (" << (int)sFlow->routeId
              << ") SendEmptyPacket(" <<TcpFlagPrinter(flags) << ") hasSyn -> Connection failed."
              << " Subflow's state: " << TcpStateName[sFlow->state] << " Connection's state: "
              << TcpStateName[m_state] << " NumSubflows: " << subflows.size() << " SendingBuffer: "
              << sendingBuffer.PendingData() << " SubflowBufferSize: "<< sFlow->mapDSN.size());

          // If intial subflow stuck on establishing a connection then close entire endpoint!
          if (subflows.size () == 1)
            { // If there is only one subflow we can safely tear down entire connection
              CloseAndNotifyAllSubflows ();
              return;
            }

          CloseAndNotify (sFlow->routeId); // what if only one subflow failed to connect??
          return;
        }
      else
        { // Exponential backoff of connection time out
          int backoffCount = 0x1 << (sFlow->cnRetries - sFlow->cnCount);
          RTO = Seconds (sFlow->cnTimeout.GetSeconds () * backoffCount);
          sFlow->cnCount = sFlow->cnCount - 1;
          NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " ["<< m_node->GetId() << "] ("<< (int)sFlow->routeId<< ") " << flowType << " SendEmptyPacket -> backoffCount: " << backoffCount << " RTO: " << RTO.GetSeconds() << " cnTimeout: " << sFlow->cnTimeout.GetSeconds() <<" cnCount: "<< sFlow->cnCount);
        }
    }
  if (((sFlow->state == SYN_SENT) || (sFlow->state == SYN_RCVD && mpEnabled == true)) && mpSendState == MP_NONE)
    {
      mpSendState = MP_MPC;                  // This state means MP_MPC is sent
      do
        { // Prevent repetition of localToken to a node
          localToken = rand ();        // Random Local Token
        }
      while (m_tcp->m_TokenMap.count (localToken) != 0 || localToken == 0);
      NS_ASSERT(m_tcp->m_TokenMap.count (localToken) == 0 && localToken != 0);
      header.AddOptMPC (OPT_MPC, localToken); // Adding MP_CAPABLE & Token to TCP option (5 Bytes)
      olen += 5;
      m_tcp->m_TokenMap[localToken] = m_endPoint;       //m_tcp->m_TokenMap.insert(std::make_pair(localToken, m_endPoint))
      NS_LOG_UNCOND("["<< m_node->GetId() << "] ("<< (int)sFlow->routeId<< ") SendEmptyPacket -> LOCALTOKEN is mapped to connection endpoint -> " << localToken << " -> " << m_endPoint << " TokenMapsSize: "<< m_tcp->m_TokenMap.size());
    }
  else if ((sFlow->state == SYN_SENT && hasSyn && sFlow->routeId == 0)/* || (sFlow->state == SYN_RCVD && hasSyn && sFlow->routeId == 0)*/)
    {
      header.AddOptMPC (OPT_MPC, localToken);       // Adding MP_CAPABLE & Token to TCP option (5 Bytes)
      olen += 5;
    }
  else if (sFlow->state == SYN_SENT && hasSyn && sFlow->routeId != 0)
    {
      header.AddOptJOIN (OPT_JOIN, remoteToken, 0); // addID should be zero?
      olen += 6;
    }

  uint8_t plen = (4 - (olen % 4)) % 4;
  olen = (olen + plen) / 4;
  hlen = 5 + olen;
  header.SetLength (hlen);
  header.SetOptionsLength (olen);
  header.SetPaddingLength (plen);
  // @SendEmptyPacket
  if (m_ceBit > 0 && isAck && sFlow->state == ESTABLISHED && server)
    AddPacketTag (p, ECE_TAG);

  // @SendEmptyPacket -> Add control packet tag
  if (hasSyn || hasFin || (isAck && client))
    AddPacketTag (p, CP_TAG); //AddContPktTag(p);

  if (m_isDCTCPEnabled || m_ecn)
    AddPacketTag (p, ECT_TAG); //AddEctTag(p);

  m_tcp->SendPacket (p, header, sFlow->sAddr, sFlow->dAddr, FindOutputNetDevice (sFlow->sAddr));
  //sFlow->rtt->SentSeq (sFlow->TxSeqNumber, 1);           // notify the RTT

  if (sFlow->retxEvent.IsExpired () && (hasFin || hasSyn) && !isAck)
    { // Retransmit SYN / SYN+ACK / FIN / FIN+ACK to guard against lost
      //RTO = sFlow->rtt->RetransmitTimeout();
      sFlow->retxEvent = Simulator::Schedule (RTO, &MpTcpSocketBase::SendEmptyPacket, this, sFlowIdx, flags);
      if (hasSyn)
        {
          //cout << this << " ["<< m_node->GetId() << "]("<<(int)sFlowIdx <<") SendEmptyPacket -> "<< TcpFlagPrinter(flags)<< " ReTxTimer set for SYN / SYN+ACK now " << Simulator::Now ().GetSeconds () << " Expire at " << (Simulator::Now () + RTO).GetSeconds () << " RTO: " << RTO.GetSeconds() << " FlowType: " << flowType << " Header: "<< header << endl;
          NS_LOG_UNCOND (this << " ["<< m_node->GetId() << "]("<<(int)sFlowIdx <<") SendEmptyPacket -> "<< TcpFlagPrinter(flags)<< " ReTxTimer set for SYN / SYN+ACK now " << Simulator::Now ().GetSeconds () << " Expire at " << (Simulator::Now () + RTO).GetSeconds () << " RTO: " << RTO.GetSeconds() << " FlowType: " << flowType << " Header: "<< header);
        }
      if (hasFin)
        {
          NS_LOG_UNCOND (this << " ["<< m_node->GetId() << "]("<<(int)sFlowIdx <<") SendEmptyPacket -> "<<TcpFlagPrinter(flags)<< " ReTxTimer set for FIN / FIN+ACK now " << Simulator::Now ().GetSeconds () << " Expire at " << (Simulator::Now () + RTO).GetSeconds () << " RTO: " << RTO.GetSeconds() << " FlowType: " << flowType << " Header: " << header);
        }
    }

  //if (!isAck)
  NS_LOG_INFO("("<< (int)sFlowIdx<<") SendEmptyPacket-> "<< header <<" Length: "<< (int)header.GetLength());
}

//void
//MpTcpSocketBase::allocateSendingBuffer(uint32_t size)
//{
//  NS_LOG_FUNCTION(this << size);
//  sendingBuffer = new DataBuffer(size);
//}
//
//void
//MpTcpSocketBase::allocateRecvingBuffer(uint32_t size)
//{
//  NS_LOG_FUNCTION(this << size);
//  recvingBuffer = new DataBuffer(size);
//}
//
//void
//MpTcpSocketBase::SetunOrdBufMaxSize(uint32_t size)
//{
//  NS_LOG_FUNCTION_NOARGS();
//  //unOrdMaxSize = size;
//}

void
MpTcpSocketBase::SetSndBufSize (uint32_t size)
{
  //m_txBuffer.SetMaxBufferSize(size);
  //sendingBuffer = new DataBuffer(size);
  sendingBuffer.SetBufferSize (size);
}
uint32_t
MpTcpSocketBase::GetSndBufSize (void) const
{
  //return m_txBuffer.MaxBufferSize();
  return 0;
}
void
MpTcpSocketBase::SetRcvBufSize (uint32_t size)
{
  //m_rxBuffer.SetMaxBufferSize(size);
  //recvingBuffer = new DataBuffer(size);
  // Size of recving buffer does not allocate any memory instantly but allows node to store to this bound.
  recvingBuffer.SetBufferSize (50000000);
}
uint32_t
MpTcpSocketBase::GetRcvBufSize (void) const
{
  //return m_rxBuffer.MaxBufferSize();
  return 0;
}

uint32_t
MpTcpSocketBase::Recv (uint32_t size)
{
  NS_LOG_FUNCTION (this);
  //Null packet means no data to read, and an empty packet indicates EOF
  uint32_t toRead = std::min (recvingBuffer.PendingData (), size);
  return recvingBuffer.Retrieve (toRead);
}
void
MpTcpSocketBase::ForwardUp (Ptr<Packet> p, Ipv4Header header, uint16_t port, Ptr<Ipv4Interface> interface)
{

  NS_LOG_FUNCTION_NOARGS();
  DoForwardUp (p, header, port, interface);
}
void
MpTcpSocketBase::ExtractPacketTags (Ptr<Packet> p)
{
  m_ceBit = 0;
  m_eceBit = 0;
  CeTag tag;
  bool done = p->RemovePacketTag (tag);
  if (done)
    {
      m_ceBit = tag.GetCe ();
    }
  EceTag ece;
  done = p->RemovePacketTag (ece);
  if (done)
    {
      m_eceBit = ece.GetEce ();
    }
  ControlTag ct; // Just remove it from packet.
  p->RemovePacketTag (ct);
  EctTag ect;
  p->RemovePacketTag(ect);
  EcmpTag ecmp;
  p->RemovePacketTag(ecmp);
}
void
MpTcpSocketBase::DoForwardUp (Ptr<Packet> p, Ipv4Header header, uint16_t port, Ptr<Ipv4Interface> interface)
{
  if (m_endPoint == 0)
    {
      NS_LOG_UNCOND("No endpoint exist");
      return;
    }NS_LOG_FUNCTION(this<< " SubflowSize["<<subflows.size() << "]");
  Address fromAddress = InetSocketAddress (header.GetSource (), port);
  Address toAddress = InetSocketAddress (header.GetDestination (), m_endPoint->GetLocalPort ());

  m_localAddress = header.GetDestination ();
  m_remoteAddress = header.GetSource ();

  // Peel off TCP header and do validity checking
  TcpHeader mptcpHeader;
  p->RemoveHeader (mptcpHeader);

  // DCTCP
  ExtractPacketTags(p);

  m_remotePort = port;
  m_localPort = mptcpHeader.GetDestinationPort ();

  // This is make sense as subSock' local port might be different from metaSocket's localport!!
  // NS_ASSERT_MSG(m_localPort == m_endPoint->GetLocalPort(), " localPort: " << m_localPort << " ePointLocal: " << m_endPoint->GetLocalPort());

  // Listening socket being dealt with here......
  if (subflows.size () == 0 && m_state == LISTEN)
    {
      NS_ASSERT(server && m_state == LISTEN); NS_LOG_UNCOND("Listening socket receives SYN packet, it need to be CLONED... " << mptcpHeader);
      // Update the flow control window
      remoteRecvWnd = (uint32_t) mptcpHeader.GetWindowSize ();
      // We need to define another ReadOption with no subflow in it
      if (ReadOptions (p, mptcpHeader) == false)
        return;
      // We need to define another ProcessListen with no subflow in it
      NS_ASSERT(m_endPoint->GetLocalPort () == mptcpHeader.GetDestinationPort ());
      ProcessListen (p, mptcpHeader, fromAddress, toAddress);
      // Reset all variables after cloning is ended to ready for next connection
      mpRecvState = MP_NONE;
      mpEnabled = false;
      remoteToken = 0;
      localToken = 0;
      remoteRecvWnd = 1;
      return;
    }
   int sFlowIdx = LookupSubflow (m_localAddress, m_localPort, m_remoteAddress, m_remotePort);

  if (client && sFlowIdx > maxSubflows)
    exit (20);
  NS_ASSERT_MSG(sFlowIdx <= maxSubflows, "Subflow number should be smaller than MaxNumOfSubflows");
  NS_ASSERT_MSG(
      sFlowIdx >= 0,
      "sFlowIdx is -1, i.e., invalid packet received - This is not a bug we need to deal with it - sFlowIdx: "<< sFlowIdx);

  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  //uint32_t dataLen;   // packet's payload length
  remoteRecvWnd = (uint32_t) mptcpHeader.GetWindowSize (); //update the flow control window

  if (mptcpHeader.GetFlags () & TcpHeader::ACK)
    { // This function update subflow's lastMeasureRtt variable.
      EstimateRtt (sFlowIdx, mptcpHeader);
    }

  if (ReadOptions (sFlowIdx, p, mptcpHeader) == false)
    return;

// TCP state machine code in different process functions
// C.f.: tcp_rcv_state_process() in tcp_input.c in Linux kernel
  currentSublow = sFlow->routeId;
  switch (sFlow->state)
    {
  case ESTABLISHED:
    ProcessEstablished (sFlowIdx, p, mptcpHeader);
    break;
  case LISTEN:
    ProcessListen (sFlowIdx, p, mptcpHeader, fromAddress, toAddress);
    break;
  case TIME_WAIT:
// Do nothing
    break;
  case CLOSED:
    NS_LOG_INFO(" ("<< sFlow->routeId << ") " << TcpStateName[sFlow->state] << " -> Send RST");
// Send RST if the incoming packet is not a RST
    if ((mptcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG)) != TcpHeader::RST)
      { // Since sFlow->m_endPoint is not configured yet, we cannot use SendRST here
        cout << Simulator::Now ().GetSeconds () << " [" << m_node->GetId () << "] (" << (int) sFlowIdx << ") {" << flowId
            << "} SendRST(DoForwardup)" << endl;
        TcpHeader h;
        h.SetFlags (TcpHeader::RST);
        h.SetSequenceNumber (SequenceNumber32 (sFlow->TxSeqNumber));
        h.SetAckNumber (SequenceNumber32 (sFlow->RxSeqNumber));
        h.SetSourcePort (sFlow->sPort);
        h.SetDestinationPort (sFlow->dPort);
        h.SetWindowSize (AdvertisedWindowSize ());
        m_tcp->SendPacket (Create<Packet> (), h, header.GetDestination (), header.GetSource (),
                           FindOutputNetDevice (header.GetDestination ()));
      }
    break;
  case SYN_SENT:
    ProcessSynSent (sFlowIdx, p, mptcpHeader);
    break;
  case SYN_RCVD:
    ProcessSynRcvd (sFlowIdx, p, mptcpHeader, fromAddress, toAddress);
    break;
  case FIN_WAIT_1:
  case FIN_WAIT_2:
  case CLOSE_WAIT:
    ProcessWait (sFlowIdx, p, mptcpHeader);
    break;
  case CLOSING:
//ProcessClosing(sFlowIdx, p, mptcpHeader);
    break;
  case LAST_ACK:
    ProcessLastAck (sFlowIdx, p, mptcpHeader);
    break;
  default:
// mute compiler
    break;
    }
}
/*
 * This function is used to initiate new subflow. At the moment when there is a connection via an IP address, then it does not open any new subflow with this address again.
 * For example if you need four subflows, you need to have four IP addresses!
 * Note that SetDestroyCallback(), for each subflow's endpoint, is not setup since MPTCP connection only can be closed when all subflows are closed!
 * We are planning to change this, in line with RFC 6824 in near future!
 */
bool
MpTcpSocketBase::InitiateSubflows ()
{
  NS_LOG_FUNCTION_NOARGS(); //
  NS_LOG_DEBUG("----------------------------- InitiateSubflows By Client ---------------------------");

  for (uint32_t i = 0; i < localAddrs.size (); i++)
    for (uint32_t j = i; j < remoteAddrs.size (); j++)
      {
        uint8_t addrID = localAddrs[i]->addrID;
        Ipv4Address local = localAddrs[i]->ipv4Addr;
        Ipv4Address remote = remoteAddrs[j]->ipv4Addr;

        // skip already established flows and if there is no route between a pair
        if (((local == m_localAddress) || (remote == m_remoteAddress)) || (!IsThereRoute (local, remote)))
          {
            NS_LOG_INFO("InitiateSubflows -> Skip subflow which is already established or has not a route (" << local << " -> " << remote<<")");
            continue;
          }NS_LOG_LOGIC ("IsThereRoute() -> Route from src "<< local << " to dst " << remote <<", exist !");

        // Create new subflow
        Ptr<MpTcpSubFlow> sFlow = CreateObject<MpTcpSubFlow> ();
        sFlow->routeId = (subflows.size () == 0 ? 0 : subflows[subflows.size () - 1]->routeId + 1);

        // Set up subflow local addrs:port from its endpoint
        sFlow->sAddr = local;
        sFlow->sPort = m_endPoint->GetLocalPort ();
        sFlow->dAddr = remote;
        sFlow->dPort = m_remotePort; // TODO Is this right?
        sFlow->MSS = segmentSize;
        sFlow->cwnd = sFlow->MSS;               // We should do this ... since cwnd is 0
        sFlow->state = SYN_SENT;
        sFlow->cnTimeout = m_cnTimeout;
        sFlow->cnRetries = m_cnRetries;
        sFlow->cnCount = sFlow->cnRetries;
        sFlow->m_endPoint = m_tcp->Allocate (sFlow->sAddr, sFlow->sPort, sFlow->dAddr, sFlow->dPort); // Insert New Subflow to the list
        if (sFlow->m_endPoint == 0)
          return -1;
        sFlow->m_endPoint->SetRxCallback (MakeCallback (&MpTcpSocketBase::ForwardUp, Ptr<MpTcpSocketBase> (this)));
        subflows.insert (subflows.end (), sFlow);

        // Create packet and add MP_JOIN option to it.
        Ptr<Packet> pkt = Create<Packet> ();
        TcpHeader header;
        header.SetFlags (TcpHeader::SYN);
        header.SetSequenceNumber (SequenceNumber32 (sFlow->TxSeqNumber));
        header.SetAckNumber (SequenceNumber32 (sFlow->RxSeqNumber));
        header.SetSourcePort (sFlow->sPort);
        header.SetDestinationPort (sFlow->dPort);
        header.SetWindowSize (AdvertisedWindowSize ());
        header.AddOptJOIN (OPT_JOIN, remoteToken, addrID);
        uint8_t olen = 6;
        uint8_t plen = (4 - (olen % 4)) % 4;
        olen = (olen + plen) / 4;
        uint8_t hlen = 5 + olen;
        header.SetLength (hlen);
        header.SetOptionsLength (olen);
        header.SetPaddingLength (plen);
        NS_LOG_ERROR("InitiateSubflow-> hLen: " << (int) hlen);

        // Send packet lower down the networking stack
        m_tcp->SendPacket (pkt, header, local, remote, FindOutputNetDevice (local));
        NS_LOG_INFO("InitiateSubflows -> (" << local << " -> " << remote << ") | "<< header);
      }
  return true;
}

bool
MpTcpSocketBase::InitiateSingleSubflows (uint16_t randomPort)
{
  NS_LOG_FUNCTION_NOARGS();

  //NS_ASSERT(pathManager == NdiffPorts);
//  for (uint32_t i = 0; i < maxSubflows; i++)
//    {
// Create new subflow
  //randomPort = ((rand() * rand()) % 65536);
  //NS_LOG_UNCOND(m_node->GetId() << " randomPort: " << randomPort);
  Ptr<MpTcpSubFlow> sFlow = CreateObject<MpTcpSubFlow> ();
  sFlow->routeId = (subflows.size () == 0 ? 0 : subflows[subflows.size () - 1]->routeId + 1);
  // Set up subflow based on different source ports
  sFlow->sAddr = m_endPoint->GetLocalAddress ();
  sFlow->sPort = randomPort + subflows.size (); // TODO: This could be return to its original value
  sFlow->dAddr = m_endPoint->GetPeerAddress ();
  sFlow->dPort = m_endPoint->GetPeerPort ();
  sFlow->MSS = segmentSize;
  sFlow->cwnd = sFlow->MSS;
  sFlow->state = SYN_SENT;
  sFlow->cnTimeout = m_cnTimeout;
  sFlow->cnRetries = m_cnRetries;
  sFlow->cnCount = sFlow->cnRetries;
  sFlow->m_endPoint = m_tcp->Allocate (sFlow->sAddr, sFlow->sPort, sFlow->dAddr, sFlow->dPort);
  if (sFlow->m_endPoint == 0)
    return -1;
  sFlow->m_endPoint->SetRxCallback (MakeCallback (&MpTcpSocketBase::ForwardUp, Ptr<MpTcpSocketBase> (this)));
  subflows.insert (subflows.end (), sFlow);

  // Create packet and add MP_JOIN option to it.
  Ptr<Packet> pkt = Create<Packet> ();
  TcpHeader header;
  header.SetFlags (TcpHeader::SYN);
  header.SetSequenceNumber (SequenceNumber32 (sFlow->TxSeqNumber));
  header.SetAckNumber (SequenceNumber32 (sFlow->RxSeqNumber));
  header.SetSourcePort (sFlow->sPort);
  header.SetDestinationPort (sFlow->dPort);
  header.SetWindowSize (AdvertisedWindowSize ());
  header.AddOptJOIN (OPT_JOIN, remoteToken, /*addrID*/0);
  uint8_t olen = 6;
  uint8_t plen = (4 - (olen % 4)) % 4;
  olen = (olen + plen) / 4;
  uint8_t hlen = 5 + olen;
  header.SetLength (hlen);
  header.SetOptionsLength (olen);
  header.SetPaddingLength (plen);
  NS_LOG_ERROR("InitiateSubflow-> hLen: " << (int) hlen);  //
  NS_LOG_UNCOND(this << " => "<< Simulator::Now().GetSeconds() <<" [" << m_node->GetId() <<"] (" <<sFlow->routeId << ") InitiateSingleSubflow -> 4-Tuple: " << sFlow->sAddr<< ":"<< sFlow->sPort << " , "<< sFlow->dAddr << ":" << sFlow->dPort);
  // cout << Simulator::Now().GetSeconds() <<" [" << m_node->GetId() <<"] (" <<sFlow->routeId << ") InitiateSingleSubflow -> 4-Tuple: " << sFlow->sAddr<< ":"<< sFlow->sPort << " , "<< sFlow->dAddr << ":" << sFlow->dPort << endl;
  // @InitiateSingleSubflows -> Add control packet
  AddPacketTag(pkt, CP_TAG); //AddContPktTag(pkt);
  // Send packet lower down the networking stack
  m_tcp->SendPacket (pkt, header, sFlow->sAddr, sFlow->dAddr, FindOutputNetDevice (sFlow->sAddr));
//    }
  return true;
}

#ifdef RAND_GAP
void
MpTcpSocketBase::InitiateMultipleSubflows ()
{
  NS_LOG_FUNCTION_NOARGS();
  //Ptr<UniformRandomVariable> uniRandom = CreateObject<UniformRandomVariable>();
  vector<uint32_t> randomStorage;
  uint16_t randomPort = 0;
  randomPort = rand () % 65000;
  if (randomPort == m_endPoint->GetLocalPort ())
    {
      NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] This is a rear condition where a generated random port is the same as initial subflow's local port!");
      randomPort++;
    }
  double randomGap = 0;
  for (int i = 1; i < maxSubflows; i++)
    {
      do
        {
          //randomGap = uniRandom->GetInteger(0, 50);
          randomGap = rand () % m_rGap;
        }
      while (std::find (randomStorage.begin (), randomStorage.end (), randomGap) != randomStorage.end ());
      randomStorage.push_back (randomGap);

      NS_LOG_UNCOND (Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << i <<") scheduled for " << randomGap << "us");
      //cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << i <<") scheduled for " << randomGap << " us with rGap: "  << m_rGap << endl;
      Simulator::Schedule (MicroSeconds (randomGap), &MpTcpSocketBase::InitiateSingleSubflows, this, randomPort);
    }
}
#endif

#ifdef DRAND_GAP
void
MpTcpSocketBase::InitiateMultipleSubflows()
  {
    NS_LOG_FUNCTION_NOARGS();
    //Ptr<UniformRandomVariable> uniRandom = CreateObject<UniformRandomVariable>();
    vector<uint32_t> randomStorage;
    uint16_t randomPort = 0;
    randomPort = rand() % 65000;
    if (randomPort == m_endPoint->GetLocalPort())
      {
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] This is a rear condition where a generated random port is the same as initial subflow's local port!");
        randomPort++;
      }
    double randomGap, tmp = 0;
    for (int i = 1; i < maxSubflows; i++)
      {
        do
          {
            //randomGap = uniRandom->GetInteger(0, 50);
            //randomGap = rand() % 50;
            tmp = drand();
            randomGap = m_rGap * tmp;
          }
        while (std::find(randomStorage.begin(), randomStorage.end(), randomGap) != randomStorage.end());
        randomStorage.push_back(randomGap);
        NS_LOG_UNCOND (Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << i <<") scheduled for " << randomGap << " ms");
        //cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] (" << i << ") scheduled for " << randomGap << " ms" << " drand: " << tmp  << " rGap: " << m_rGap << endl;
        Simulator::Schedule(MilliSeconds(randomGap), &MpTcpSocketBase::InitiateSingleSubflows, this, randomPort);
      }
  }
#endif

void
MpTcpSocketBase::calculateTotalCWND ()
{
  totalCwnd = 0;
  if (m_isAdaptiveSubflow && IncastDetected())
    { // Look at subflow zero only as it should only be activated now...
      assert(maxSubflows >= 2);
      if (subflows[0]->m_inFastRec)
        totalCwnd += subflows[0]->ssthresh;
      else
        totalCwnd += subflows[0]->cwnd.Get ();  // Should be this all the time
    }
  else
    {
      for (uint32_t i = 0; i < subflows.size (); i++)
        {
          if (subflows[i]->m_inFastRec)
            totalCwnd += subflows[i]->ssthresh;
          else
            totalCwnd += subflows[i]->cwnd.Get ();  // Should be this all the time
        }
    }
}

uint32_t
MpTcpSocketBase::compute_total_window ()
{
  if (AlgoCC < COUPLED_SCALABLE_TCP)
    exit (10);

  totalCwnd = 0;
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      if (subflows[i]->m_inFastRec)
        totalCwnd += subflows[i]->ssthresh;
      else
        totalCwnd += subflows[i]->cwnd.Get ();
    }
  return totalCwnd;
}

void
MpTcpSocketBase::ReadUnOrderedData ()
{
  NS_LOG_FUNCTION (this);
  //NS_LOG_WARN("ReadUnOrderedData()-> Size: " << unOrdered.size());
  list<DSNMapping *>::iterator current = unOrdered.begin ();
  list<DSNMapping *>::iterator next = unOrdered.begin ();

  // I changed this method, now whenever a segment is readed it get dropped from that list
  while (next != unOrdered.end ())
    {
      ++next;
      DSNMapping *ptrDSN = *current;
      uint32_t sFlowIdx = ptrDSN->subflowIndex;
      Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
      if ((ptrDSN->dataSeqNumber <= nextRxSequence) /*&& (ptrDSN->subflowSeqNumber == sFlow->RxSeqNumber)*/)
        { /* Stored segment is in-order at connection level */
          NS_ASSERT(ptrDSN->dataSeqNumber == nextRxSequence);

          //uint32_t amount = recvingBuffer->Add(ptrDSN->packet, ptrDSN->dataLevelLength);
          uint32_t amount = recvingBuffer.Add (ptrDSN->dataLevelLength);
          if (amount == 0)
            { // Receive buffer is full.
              NS_FATAL_ERROR("In our model receive buffer never get full");
              break;
            }
          NS_ASSERT(amount == ptrDSN->dataLevelLength);
          nextRxSequence += amount;

          if (ptrDSN->subflowSeqNumber == sFlow->RxSeqNumber)
            { /** Stored segment is also in-order at sub-flow level */
              sFlow->RxSeqNumber += amount;
              sFlow->highestAck = std::max (sFlow->highestAck, ptrDSN->acknowledgement - 1);
              //SendEmptyPacket(sFlowIdx, TcpHeader::ACK);
              sFlow->AccumulativeAck = true; //TODO TEMP
            }
          else
            NS_ASSERT(ptrDSN->subflowSeqNumber < sFlow->RxSeqNumber);

          NotifyDataRecv ();
          unOrdered.erase (current);
          delete ptrDSN;
        }
      else if (ptrDSN->subflowSeqNumber == sFlow->RxSeqNumber)
        { /* Stored segment is in-order only at sub-flow level! */
          NS_ASSERT((ptrDSN->dataSeqNumber > nextRxSequence));
          //NS_LOG_UNCOND("ReadUnOrderedData()-> sub-flow is in-order but connection is out of order " << (int)sFlow->routeId);
          sFlow->RxSeqNumber += ptrDSN->dataLevelLength;
          sFlow->highestAck = std::max (sFlow->highestAck, ptrDSN->acknowledgement - 1);
          // TODO Should let sender know about this update ?!?!
          // ACK should be sent per packet basis! If we send any ACK here it would break this rule? Could we solve this via DATA-ACK?
          sFlow->AccumulativeAck = true;  // TODO TEMP
          //SendEmptyPacket(sFlowIdx, TcpHeader::ACK);
        }
      current = next;
    }
}

uint8_t
MpTcpSocketBase::ProcessOption (TcpOptions * opt)
{
  uint8_t originalSFlow = 255;
  if (opt != 0)
    {

    }
  return originalSFlow;
}

/*
 * When dupAckCount reach to the default value of 3 then TCP goes to ack recovery process.
 */
void
MpTcpSocketBase::DupAck (uint8_t sFlowIdx, DSNMapping* ptrDSN)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  sFlow->m_dupAckCount++;
  ptrDSN->dupAckCount++; // Used for evaluation purposes only
  uint32_t segmentSize = sFlow->MSS;
  //calculateTotalCWND();

#ifdef PLOT
  uint32_t tmp = (((ptrDSN->subflowSeqNumber) - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->DUPACK.push_back (make_pair (Simulator::Now ().GetSeconds (), tmp));
#endif

  // Congestion control algorithms
  if (sFlow->m_dupAckCount == 3 && !sFlow->m_inFastRec)
    { // FastRetrasmsion
      NS_LOG_WARN (Simulator::Now().GetSeconds() <<" DupAck -> Subflow ("<< (int)sFlowIdx <<") 3rd duplicated ACK for segment ("<<ptrDSN->subflowSeqNumber<<")");

      if (m_dctcpFastReTxRecord)
        { // danger: m_dctcpFastReTxRecord should be deactivated in normal run
          uint32_t oldCwnd = sFlow->cwnd.Get();
          RecordDctcpFastRetx(sFlowIdx, oldCwnd);
        }

      // Cut the window to the half
      ReduceCWND (sFlowIdx, ptrDSN);

#ifdef PLOT
      sFlow->_FReTx.push_back (make_pair (Simulator::Now ().GetSeconds (), TimeScale));
#endif
      FastReTxs++;
    }
  else if (sFlow->m_inFastRec)
    { // Fast Recovery
// Increase cwnd for every additional DupACK (RFC2582, sec.3 bullet #3)
      sFlow->cwnd += segmentSize;
      sFlow->m_duplicatesSize += m_segmentSize;

#ifdef PLOT
      DupAcks.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->cwnd));
      sFlow->ssthreshtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->ssthresh));
#endif
      NS_LOG_WARN ("DupAck-> FastRecovery. Increase cwnd by one MSS, from " << sFlow->cwnd.Get() <<" -> " << sFlow->cwnd << " AvailableWindow: " << AvailableWindow(sFlowIdx));
      FastRecoveries++;
      // Send more data into pipe if possible to get ACK clock going
      SendPendingData (sFlow->routeId); // dupack()
    }
  else
    {
      NS_LOG_WARN("Limited transmit is not enabled... DupAcks: " << ptrDSN->dupAckCount);
    }

}

std::string
MpTcpSocketBase::PrintCC (uint32_t cc)
{
  switch (cc)
    {
  case 0:
    return "UC"; //0
    break;
  case 1:
    return "LI"; //1
    break;
  case 2:
    return "RC"; //2
    break;
  case 3:
    return "FC"; //3
    break;
  case 4:
    return "CST";            //4
    break;
  case 5:
    return "UC";             //5
    break;
  case 6:
    return "CE";             //6
    break;
  case 7:
    return "CI";             //7
    break;
  case 8:
    return "CF";             //8
    break;
  case 9:
    return "FU";            //9
    break;
  case 10:
    return "FI";            //10
    break;
  case 11:
    return "XCA";           //11
    break;
  default:
    exit (200);
    return "Unknown";
    break;
    }
  exit (200);
  return "Unknown";
}

uint32_t
MpTcpSocketBase::GetTotalPktSent()
{
  uint32_t pkts = 0;
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      pkts += subflows[i]->PktCount;
    }
  return pkts;
}

string
MpTcpSocketBase::CutFileNameOnly ()
{
  std::string tmp = outputFileName;
  std::string cut = "_RESULT_";
  std::string::size_type pos = tmp.find (cut);
  if (pos != std::string::npos)
    tmp.erase (pos, cut.length ());
  // remove .data extension as well, if exist.
  cut = ".data";
  pos = tmp.find (cut);
  if (pos != std::string::npos)
    tmp.erase (pos, cut.length ());
  return tmp;
}

string
MpTcpSocketBase::CutFileName (std::string::size_type &position)
{
  std::string tmp = outputFileName;
  std::string cut = "_RESULT_";
  std::string::size_type pos = tmp.find (cut);
  if (pos != std::string::npos)
    tmp.erase (pos, cut.length ());
  position = pos;
  // remove .data extension as well, if exist.
  cut = ".data";
  pos = tmp.find (cut);
  if (pos != std::string::npos)
    tmp.erase (pos, cut.length ());
  return tmp;
}

void
MpTcpSocketBase::GenerateRateTracerSf()
{
  std::string::size_type pos = std::string::npos;
  string fileName = "";
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      subflows[i]->nextRateEvent.Cancel ();

      fileName = CutFileName (pos);
      if (pos != std::string::npos)
        {
          stringstream oss;
          oss << "_RATE_"  << m_node->GetId () << "_" << flowId << "_SF_" << i << "_";
          fileName.insert (pos, oss.str());
          fileName += ".data";
          oss.str("");
        }
      else
        {
          stringstream oss;
          oss << i;
          fileName = CutFileName (pos) + "_RATE_SF_" + oss.str () + ".data";
          oss.str("");
        }

      Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (fileName, std::ios::out);
      ostream* os = stream->GetStream ();

      vector<pair<double, double> >::iterator it = subflows[i]->rateTracerSf.begin ();
      for (uint32_t j = 0; j < 30 && it != subflows[i]->rateTracerSf.end (); j++)
        *os << it->first << "\t" << 0.0 << endl;

      it = subflows[i]->rateTracerSf.begin ();
      while (it != subflows[i]->rateTracerSf.end ())
        {
          *os << it->first << "\t" << it->second << endl;
          it++;
        }
      //double lastSampledGoodput = (((subflows[i]->TxSeqNumber - subflows[i]->lastTxSeqNumer) * 8) / m_rateInterval);
      double lastSampledGoodput = ((subflows[i]->totalSentByte* 8) / m_rateInterval);
      *os << Simulator::Now ().GetSeconds () << "\t" << lastSampledGoodput / 1000000 << endl;
      for (int i = 0; i < 30; i++)
        *os << Simulator::Now ().GetSeconds () << "\t" << 0.0 << endl;
    }
}

void
MpTcpSocketBase::GenerateRateTracerCl ()
{
  m_nextRateEvent.Cancel ();

  std::string::size_type pos = std::string::npos;
  string fileName = CutFileName (pos);
  if (pos != std::string::npos)
    {
      ostringstream oss;
      oss << "_RATE_" << m_node->GetId() << "_" << flowId << "_CL_";
      fileName.insert (pos, oss.str());
      fileName += ".data";
      oss.str("");
    }
  else
    fileName = CutFileName (pos) + "_RATE_CL.data";

  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (fileName, std::ios::out);
  ostream* os = stream->GetStream ();

  vector<pair<double, double> >::iterator it = rateTracerCl.begin ();
  for (uint32_t j = 0; j < 30 && it != rateTracerCl.end (); j++)
    *os << it->first << "\t" << 0.0 << endl;

  it = rateTracerCl.begin ();
  while (it != rateTracerCl.end ())
    {
      *os << it->first << "\t" << it->second << endl;
      it++;
    }
  //double lastSampledGoodput = (((nextTxSequence - lastNextTxSequence) * 8) / m_rateInterval);
  double lastSampledGoodput = ((m_totalSentBytes * 8) / m_rateInterval);
  *os << Simulator::Now ().GetSeconds () << "\t" << lastSampledGoodput / 1000000 << endl;
  for (int i = 0; i < 30; i++)
    *os << Simulator::Now ().GetSeconds () << "\t" << 0.0 << endl;
}

void
MpTcpSocketBase::GenerateTotalCwnd ()
{
  std::string::size_type pos = std::string::npos;
  string fileName = CutFileName (pos);
  if (pos != std::string::npos)
    {
      ostringstream oss;
      oss << "_CWND_" << m_node->GetId () << "_" << flowId << "_TOTAL_";
      fileName.insert (pos, oss.str ());
      fileName += ".data";
      oss.str ("");
    }
  else
    fileName = CutFileName (pos) + "_CWND_TOTAL.data";

  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (fileName, std::ios::out);
  ostream* os = stream->GetStream ();

  vector<pair<double, double> >::iterator it = totalCWNDtrack.begin ();
  it = totalCWNDtrack.begin ();
  while (it != totalCWNDtrack.end ())
    {
      *os << it->first << "\t" << it->second << endl;
      it++;
    }
}

void
MpTcpSocketBase::GenerateCwnd ()
{
  std::string::size_type pos = std::string::npos;
  string fileName = "";
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      fileName = CutFileName (pos);
      if (pos != std::string::npos)
        {
          stringstream oss;
          oss << "_CWND_" << m_node->GetId () << "_" << flowId << "_SF_" << i << "_";
          fileName.insert (pos, oss.str ());
          fileName += ".data";
          oss.str ("");
        }
      else
        {
          stringstream oss;
          oss << i;
          fileName = CutFileName (pos) + "_CWND_SF_" + oss.str () + ".data";
          oss.str ("");
        }

      Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (fileName, std::ios::out);
      ostream* os = stream->GetStream ();

      vector<pair<double, uint32_t> >::iterator it = subflows[i]->cwndTracer.begin ();
      it = subflows[i]->cwndTracer.begin ();
      while (it != subflows[i]->cwndTracer.end ())
        {
          *os << it->first << "\t" << it->second << endl;
          it++;
        }
    }
}


void
MpTcpSocketBase::GenerateAlpha ()
{
  std::string::size_type pos = std::string::npos;
  string fileName = "";
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      fileName = CutFileName (pos);
      if (pos != std::string::npos)
        {
          stringstream oss;
          oss << "_ALPHA_" << m_node->GetId () << "_" << flowId << "_SF_" << i << "_";
          fileName.insert (pos, oss.str ());
          fileName += ".data";
          oss.str ("");
        }
      else
        {
          stringstream oss;
          oss << i;
          fileName = CutFileName (pos) + "_ALPHA_SF_" + oss.str () + ".data";
          oss.str ("");
        }

      Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (fileName, std::ios::out);
      ostream* os = stream->GetStream ();

      vector<pair<double, double> >::iterator it = subflows[i]->DCTCP_ALPHA.begin ();
      it = subflows[i]->DCTCP_ALPHA.begin ();
      while (it != subflows[i]->DCTCP_ALPHA.end ())
        {
          *os << it->first << "\t" << it->second << endl;
          it++;
        }
    }
}

string
MpTcpSocketBase::GetSocketModel()
{
  string ft;
  if (flowType == "Large")
    ft = "LF_";
  else if (flowType == "Short")
    ft = "SF_";
  else
    ft = "UN_";

  string tmpSocket;
  if (m_isDCTCPEnabled && m_slowDownEcnLike)
    {
      tmpSocket = ft + "ECN";
    }
  else if (m_isDCTCPEnabled)
    {
      tmpSocket = ft + "DCTCPv2";
    }
  else if (m_isDCTCPEnabled)
    {
      tmpSocket = ft + "DCTCP";
    }
  else
    {
      tmpSocket = ft + "UN";
    }

  if (m_ecn)
    tmpSocket += "_ECNv2";
  return tmpSocket;
}

void
MpTcpSocketBase::DoDoGenerateOutPutFile(TypeId tid)
{
  goodput = ((nextTxSequence * 8) / (Simulator::Now ().GetSeconds () - fLowStartTime));
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (outputFileName, std::ios::out | std::ios::app);
  ostream* os = stream->GetStream ();
    *os << "[:"  << m_node->GetId ()   << ":]"
        << "[+"  << flowId             << "+]"
        << "[="  << flowType           << "=]"
        << "[?"  << flowLayer          << "?]"
        << "[|"  << socketModel        << "|]"
      //<< "[."  << IsActive(m_isDCTCPEnabled)  << ".]"
        << "[#"  << goodput / 1000000  << "#]"
        << "[*"  << (Simulator::Now ().GetSeconds () - fLowStartTime)       << "*]"
        << "[$"  << TimeOuts           << "$]"
        << "[!"  << FastReTxs          << "!]"
        << "[("  << pAck               << ")]"
        << "[@"  << FullAcks           << "@]"
        << "[^"  << FastRecoveries     << "^]"
        << "[&"  << subflows.size ()   << "&]"
        << "[_"  << GetEstSubflows ()  << "_]"
        << "[%"  << tid.GetName ()     << "%]"
        << "[~"  << subflows[0]->m_retxThresh << "~]"
        << "[>"  << flowSize << "<]"
        << "[<"  << nextTxSequence 	  << ">]"
        << "[;"  << GetTotalPktSent()  << ";]"
        << "[/"  << subflows[currentSublow]->lastMeasuredRtt.GetMicroSeconds()  << "/]"
        << "[,"  << subflows[currentSublow]->rtt->GetCurrentEstimate().GetMicroSeconds()  << ",]"
        << "[{"  << GetSocketModel()   << "}]"
        << "[???"  << PrintCC(AlgoCC)    << "???]"
        << "[-"  << SlowDownHits       << "-]"
        << "[{DS"<< m_isAdaptiveSubflow   << "}]"
        << "[{IT"<< m_CongestionThreshold  << "}]"
        << "[{ET"<< m_CongestionExitThreshold << "}]"
        << "[{IC"<< m_CongestionRound    << "}]"
        << "[."  << m_CongestionEnterHits  << ".]"
        << "[???"  << m_CongestionExitHits   << "???]"
        << "[{CM"<< m_cwndMin          << "}]"
        << "[{RS"<< m_rwndScale        << "}]"
        << "[{B" << m_backoffBeta      << "_G" << m_initGamma << "}]"
        << endl;

    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " ["<< m_node->GetId()<< "] Goodput -> " << goodput / 1000000 << " Mbps");
}
// [: NodeId :][+FlowId+][= FlowType =][# Throughput #][* FlowComplTime *]
// [$ timeOut $][! fastReTx !][( PartialAck )][@ FullAck @][^ FastRecovery ^]
// [& Subflows &][% TypeId %][-RTT-][_EstSubflows_]
void
MpTcpSocketBase::DoGenerateOutPutFile ()
{
  TypeId tid = this->GetTypeId ();
  DoDoGenerateOutPutFile (tid);
}

std::string
MpTcpSocketBase::GeneratePlotDetail (void)
{
   stringstream oss;
   oss << "Node[" << m_node->GetId () << "] Flow[" << flowId << "]  Type[" << flowType << "]  CC[" << PrintCC (AlgoCC)
       << "]  DCTCP[" << m_isDCTCPEnabled << "]  ECN["<< m_slowDownEcnLike <<  "]  B[" << m_backoffBeta << "] GP[" << goodput / 1000000 << "Mbps]";
  string tmp = oss.str ();
  oss.str ("");
  return tmp;
  //stringstream detail;
  //detail << "CC:" << PrintCC(AlgoCC) << "  sF:" << subflows.size() << " C:" << LinkCapacity / 1000 << "Kbps  RTT:" << RTT
  //    << "Ms  D:" << totalBytes / 1000 << "Kb  dtQ(" << lostRate << ")  MSS:" << segmentSize << "B";
  //return detail.str();
}

std::string
MpTcpSocketBase::PlotFileNameGenerator (std::string s)
{
  stringstream oss;
  oss << s << PrintCC (AlgoCC) << "_" << (uint32_t) Simulator::Now ().GetSeconds () << "_" << (int) maxSubflows << "_"
      << GetTypeIdName () << "_" << m_node->GetId () << "_" << flowId << "_" << m_rGap;
  string tmp = oss.str ();
  oss.str("");
  return tmp;
}

void
MpTcpSocketBase::GenerateCwndTracer ()
{
  Gnuplot cwndTracerGraph;
  ostringstream oss;
  oss << "set terminal postscript eps enhanced color solid font 'Times-Bold,15'\n"
      "set output \"" << GenerateEpsFileName () << "_CWND.eps" << "\"\n"
      "set xlabel \"Time (s)\\n\" offset 0,0\n"
      "set ylabel \"Cwnd (pkts)\" offset 0,0\n"
      "set grid\n"
      "set lmargin 10.0\n"
      "set rmargin 2.0\n"
      "set yrange [:]\n"
      "set xrange [:]\n"
      "set title noenhanced\n"
      "unset key; set key bmargin center horizontal Left reverse noenhanced autotitles nobox\n";
  cwndTracerGraph.AppendExtra (oss.str());
  oss.str("");
  cwndTracerGraph.SetTitle (CutFileNameOnly() + "\\n" + GeneratePlotDetail ());

  // cwnd
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];
      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);
      std::stringstream title;
      title << "SF " << idx;
      dataSet.SetTitle (title.str ());
      vector<pair<double, uint32_t> >::iterator it = sFlow->cwndTracer.begin ();
      while (it != sFlow->cwndTracer.end ())
        {
          dataSet.Add (it->first, it->second / sFlow->MSS);
          it++;
        }
      if (sFlow->cwndTracer.size () > 0)
        cwndTracerGraph.AddDataset (dataSet);
    }

  // ssthreshold
  Gnuplot sstGraph;
  oss << "set terminal postscript eps enhanced color solid font 'Times-Bold,15'\n"
      "set output \"" << GenerateEpsFileName () << "_SST.eps" << "\"\n"
      "set xlabel\"Time (s)\\n\" offset 0,0\n"
      "set ylabel\"ssthreshold (bytes)\" offset 0,0\n"
      "set grid\n"
      "set lmargin 10.0\n"
      "set rmargin 2.0\n"
      "set yrange [:]\n"
      "set xrange [:]\n"
      "set title noenhanced\n"
      "unset key; set key bmargin center horizontal Left reverse noenhanced autotitles nobox\n";
  sstGraph.AppendExtra (oss.str());
  oss.str("");
  sstGraph.SetTitle (CutFileNameOnly() + "\\n" + GeneratePlotDetail ());

  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];
      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES);
      std::stringstream title;
      title << "SST " << idx;
      dataSet.SetTitle (title.str ());
      vector<pair<double, uint32_t> >::iterator it = sFlow->sstTracer.begin ();
      while (it != sFlow->sstTracer.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->sstTracer.size () > 0)
        sstGraph.AddDataset (dataSet);
    }
  gnu.AddPlot (cwndTracerGraph);
  gnu.AddPlot (sstGraph);
}

void
MpTcpSocketBase::GenerateRTT ()
{
  // RTT PLOT
  Gnuplot rttGraph;
  ostringstream oss;
  oss << "set terminal postscript eps enhanced color solid font 'Times-Bold,15'\n"
      "set output \"" << GenerateEpsFileName() << "_RTT.eps" << "\"\n"
      "set xlabel \"Time (s)\\n\" offset 0,0\n"
      "set ylabel \"RTT (us)\" offset 0,0 \n"
      "set grid\n"
      "set lmargin 10.0\n"
      "set rmargin 2.0\n"
      "set yrange [:]\n"
      "set xrange [:]\n"
      "set title noenhanced\n"
      "unset key; set key bmargin center horizontal Left reverse noenhanced autotitles nobox\n";
  rttGraph.AppendExtra (oss.str());
  oss.str("");
  rttGraph.SetTitle (CutFileNameOnly() + "\\n" + GeneratePlotDetail ());

  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "RTT " << idx;

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->rttTracer.begin ();

      while (it != sFlow->rttTracer.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->rttTracer.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // RTO Plot
  Gnuplot rtoGraph;
  oss << "set terminal postscript eps enhanced color solid font 'Times-Bold,15'\n"
      "set output \"" << GenerateEpsFileName () << "_RTO.eps" << "\"\n"
      "set xlabel \"Time (s)\\n\" offset 0,0\n"
      "set ylabel \"RTO (ms)\" offset 0,0 \n"
      "set grid\n"
      "set lmargin 10.0\n"
      "set rmargin 2.0\n"
      "set yrange [:]\n"
      "set xrange [:]\n"
      "set title noenhanced\n"
      "unset key; set key bmargin center horizontal Left reverse noenhanced autotitles nobox\n";
  rtoGraph.AppendExtra (oss.str());
  oss.str("");
  rtoGraph.SetTitle (CutFileNameOnly() + "\\n" + GeneratePlotDetail ());

  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "RTO " << idx;

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->rtoTracer.begin ();

      while (it != sFlow->rtoTracer.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->rtoTracer.size () > 0)
        rtoGraph.AddDataset (dataSet);
    }

  // TxQueue
  /*
   for (uint16_t idx = 0; idx < subflows.size(); idx++)
   {
   Ptr<MpTcpSubFlow> sFlow = subflows[idx];

   Gnuplot2dDataset dataSet;
   dataSet.SetStyle(Gnuplot2dDataset::LINES_POINTS);

   std::stringstream title;
   title << "TxQueue " << idx;

   dataSet.SetTitle(title.str());

   vector<pair<double, double> >::iterator it = TxQueue.begin();

   while (it != TxQueue.end())
   {
   dataSet.Add(it->first, it->second);
   it++;
   }
   rttGraph.AddDataset(dataSet);
   }
   */
  gnu.AddPlot (rttGraph);
  gnu.AddPlot (rtoGraph);
}

void
MpTcpSocketBase::GenerateRate()
{
  // RATE_SF
  Gnuplot rateSfGraph;
  ostringstream oss;
  oss << "set terminal postscript eps enhanced color solid font 'Times-Bold,15'\n"
      "set output \"" << GenerateEpsFileName() << "_RATE_SF.eps" << "\"\n"
      "set xlabel \"Time (s)\\n\" offset 0,0\n"
      "set ylabel \"Rate (Mbps)\" offset 0,0 \n"
      "set grid\n"
      "set lmargin 10.0\n"
      "set rmargin 2.0\n"
      "set title noenhanced\n"
      "set key bmargin center horizontal Left reverse noenhanced autotitles nobox\n";
  rateSfGraph.AppendExtra (oss.str ());
  oss.str("");
  rateSfGraph.SetTitle (CutFileNameOnly() + "\\n" + GeneratePlotDetail ());

  for (uint16_t idx = 0; idx < subflows.size(); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];
      sFlow->nextRateEvent.Cancel();

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle(Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "Rate " << idx;
      dataSet.SetTitle(title.str());

      vector<pair<double, double> >::iterator it = sFlow->rateTracerSf.begin();

      for (uint32_t j = 0; j < 30 && it != sFlow->rateTracerSf.end(); j++)
        dataSet.Add(it->first, 0.0);

      while (it != sFlow->rateTracerSf.end())
        {
          dataSet.Add(it->first, it->second);
          it++;
        }

      double lastSampledGoodput = ((sFlow->totalSentByte * 8) / m_rateInterval);
      dataSet.Add(Simulator::Now().GetSeconds(), (lastSampledGoodput / 1000000));

      for (int i = 0; i < 30; i++)
        dataSet.Add(Simulator::Now().GetSeconds(), 0.0);

      if (sFlow->rateTracerSf.size() > 0)
        rateSfGraph.AddDataset(dataSet);
    }
  gnu.AddPlot(rateSfGraph);

  // RATE_CL
  Gnuplot rateClGraph;
  oss << "set terminal postscript eps enhanced color solid font 'Times-Bold,15'\n"
      "set output \"" << GenerateEpsFileName() << "_RATE_CL.eps" << "\"\n"
      "set xlabel \"Time (s)\\n\" offset 0,0\n"
      "set ylabel \"Rate (Mbps)\" offset 0,0 \n"
      "set grid\n"
      "set lmargin 10.0\n"
      "set rmargin 2.0\n"
      "set title noenhanced\n"
      "set key bmargin center horizontal Left reverse noenhanced autotitles nobox\n"
      "unset key\n";
  rateClGraph.AppendExtra(oss.str());
  oss.str("");
  rateClGraph.SetTitle (CutFileNameOnly() + "\\n" + GeneratePlotDetail ());

  m_nextRateEvent.Cancel();

  Gnuplot2dDataset dataSet;
  dataSet.SetStyle(Gnuplot2dDataset::LINES_POINTS);

//std::stringstream title;
//title << "Rate ";
//dataSet.SetTitle(title.str());

  vector<pair<double, double> >::iterator it = rateTracerCl.begin();

  for (uint32_t j = 0; j < 30 && it != rateTracerCl.end(); j++)
    dataSet.Add(it->first, 0.0);

  while (it != rateTracerCl.end())
    {
      dataSet.Add(it->first, it->second);
      it++;
    }

  double lastSampledGoodput = ((m_totalSentBytes * 8) / m_rateInterval);
  dataSet.Add(Simulator::Now().GetSeconds(), (lastSampledGoodput / 1000000));

  for (int i = 0; i < 30; i++)
    dataSet.Add(Simulator::Now().GetSeconds(), 0.0);

  if (rateTracerCl.size() > 0)
    rateClGraph.AddDataset(dataSet);

  gnu.AddPlot(rateClGraph);
}

void
MpTcpSocketBase::GenerateSendvsACK ()
{
//  std::ofstream outfile (PlotFileNameGenerator ("P_").c_str());
//  Gnuplot rttGraph = Gnuplot ("data-ack.eps", GeneratePlotDetail ());
  Gnuplot rttGraph;
  ostringstream oss;
  oss << "set terminal postscript eps noenhanced color solid font 'Times-Bold,15'\n"
      "set output \"" << GenerateEpsFileName () << "_DATA_ACK.eps" << "\"\n"
      "set xlabel \"Time (s)\\n\" offset 0,0\n"
      "set ylabel \"Packet (mod 60)\" offset 0,0\n"
      "set grid xtics ytics mytics mxtics \n"
      "set mytics 2\n"
      "set mxtics 1\n"
      "set lmargin 8.0\n"
      "set rmargin 2.0\n"
      "#set tmargin 1.0\n"
      "#set bmargin 3.0\n"
      "set yrange [-2:61]\n"
      "set xrange [0:0.01]\n"
      "set title noenhanced\n"
      "unset key; set key bmargin center horizontal Left reverse noenhanced autotitles nobox spacing 1 samplen 1 font ','\n";
  rttGraph.AppendExtra (oss.str());
  oss.str("");
  rttGraph.SetTitle (CutFileNameOnly() + "\\n" + GeneratePlotDetail ());

//  std::stringstream tmp;
//  tmp << " Packet Number (Modulo " << mod << " )";
//  rttGraph.SetLegend ("Time (s)", tmp.str ());
//  rttGraph.SetTerminal ("eps");
//  rttGraph.SetExtra ("set xrange [0.0:0.1]");
//    std::stringstream t;
//  t << "set yrange [" << TimeScale - 2.0 << ":" << mod + 2 << "]";
//  rttGraph.SetExtra (t.str ());

  //DATA
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::POINTS);

      std::stringstream title;
      title << "DATA";

      dataSet.SetTitle (title.str ());

      vector<pair<double, uint32_t> >::iterator it = sFlow->DATA.begin ();

      while (it != sFlow->DATA.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->DATA.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // ACK
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::POINTS);

      std::stringstream title;
      title << "ACK";

      dataSet.SetTitle (title.str ());

      vector<pair<double, uint32_t> >::iterator it = sFlow->ACK.begin ();

      while (it != sFlow->ACK.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->ACK.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // EcnEcho
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::POINTS);

      std::stringstream title;
      title << "ECE";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->ECN_ECHO.begin ();

      while (it != sFlow->ECN_ECHO.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->ECN_ECHO.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // ECN CWND CUT POINT
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::POINTS);

      std::stringstream title;
      title << "CUT";

      dataSet.SetTitle (title.str ());
      dataSet.SetExtra(" pt 4 lc rgb '#A0522D'");

      vector<pair<double, double> >::iterator it = sFlow->ECN_CWND_CUT_POINT.begin ();

      while (it != sFlow->ECN_CWND_CUT_POINT.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->ECN_CWND_CUT_POINT.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // BEG
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::POINTS);
      dataSet.SetExtra(" ps 0.5 pt 7 lc rgb '#dd181f'");

      std::stringstream title;
      title << "BEG";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->BEG.begin ();

      while (it != sFlow->BEG.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->BEG.size () > 0)
        rttGraph.AddDataset (dataSet);
    }

  // RETRANSMIT
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::POINTS);

      std::stringstream title;
      title << "ReTx";

      dataSet.SetTitle (title.str ());

      vector<pair<double, uint32_t> >::iterator it = sFlow->RETRANSMIT.begin ();

      while (it != sFlow->RETRANSMIT.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->RETRANSMIT.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // SlowStart
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES);

      std::stringstream title;
      title << "SS";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->_ss.begin ();

      while (it != sFlow->_ss.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->_ss.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // Congestion Avoidance
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES);

      std::stringstream title;
      title << "CA";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->_ca.begin ();

      while (it != sFlow->_ca.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->_ca.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // Fast Recovery - FullACK
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "FAck";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->_FR_FA.begin ();

      while (it != sFlow->_FR_FA.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->_FR_FA.size ())
        rttGraph.AddDataset (dataSet);
    }
  // Fast Recovery - PartialACK
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "PAck";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->_FR_PA.begin ();

      while (it != sFlow->_FR_PA.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->_FR_PA.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // Fast Retransmission
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "FReTx";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->_FReTx.begin ();

      while (it != sFlow->_FReTx.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->_FReTx.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  // TimeOut
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "TO";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->_TimeOut.begin ();

      while (it != sFlow->_TimeOut.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->_TimeOut.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  //rttGraph.GenerateOutput(outfile);
  //outfile.close();
  gnu.AddPlot(rttGraph);
}

void
MpTcpSocketBase::GenerateDctcpAlpha ()
{
  Gnuplot rttGraph;
  ostringstream oss;
  oss << "set terminal postscript eps enhanced color solid font 'Times-Bold,15'\n"
      "set output \"" << GenerateEpsFileName () << "_ALPHA.eps" << "\"\n"
      "set xlabel \"Time (s)\\n\" offset 0,0\n"
      "set ylabel \"Range (#)\" offset 0,0\n"
      "set grid\n"
      "set lmargin 10.0\n"
      "set rmargin 2.0\n"
      "set yrange [0.0:1.0]\n"
      "set xrange [0.0:0.01]\n"
      "set title noenhanced\n"
      "unset key; set key bmargin center horizontal Left reverse noenhanced autotitles nobox\n";
  rttGraph.AppendExtra (oss.str());
  oss.str("");
  rttGraph.SetTitle (CutFileNameOnly() + "\\n" + GeneratePlotDetail ());

  //DCTCP ALPHA
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "Alpha";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->DCTCP_ALPHA.begin ();

      while (it != sFlow->DCTCP_ALPHA.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->DCTCP_ALPHA.size() > 0)
        rttGraph.AddDataset (dataSet);
    }
  //DCTCP FRACTION
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "Fraction";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->DCTCP_FRACTION.begin ();

      while (it != sFlow->DCTCP_FRACTION.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->DCTCP_FRACTION.size () > 0)
        rttGraph.AddDataset (dataSet);
    }
  gnu.AddPlot (rttGraph);
}

void
MpTcpSocketBase::GenerateDctcpAlphaRtt ()
{
  Gnuplot rttGraph;
  ostringstream oss;
  oss << "set terminal postscript eps enhanced color solid font 'Times-Bold,15'\n"
      "set output \"" << GenerateEpsFileName () << "_ALPHA_W_RTT.eps" << "\"\n"
      "set xlabel \"Time (s)\\n\" offset 0,0\n"
      "set ylabel \"Range (#)\" offset 0,0\n"
      "set grid\n"
      "set lmargin 10.0\n"
      "set rmargin 2.0\n"
      "set yrange [0.0:1.0]\n"
      "set xrange [0.0:0.01]\n"
      "set title noenhanced\n"
      "unset key; set key bmargin center horizontal Left reverse noenhanced autotitles nobox\n";
  rttGraph.AppendExtra (oss.str ());
  oss.str ("");
  rttGraph.SetTitle (CutFileNameOnly() + "\\n" + GeneratePlotDetail ());

  //DCTCP ALPHA
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "Alpha";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->DCTCP_ALPHA_RTT.begin ();

      while (it != sFlow->DCTCP_ALPHA_RTT.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->DCTCP_ALPHA_RTT.size() > 0)
        rttGraph.AddDataset (dataSet);
    }
  //DCTCP FRACTION
  for (uint16_t idx = 0; idx < subflows.size (); idx++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[idx];

      Gnuplot2dDataset dataSet;
      dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);

      std::stringstream title;
      title << "Fraction";

      dataSet.SetTitle (title.str ());

      vector<pair<double, double> >::iterator it = sFlow->DCTCP_FRACTION_RTT.begin ();

      while (it != sFlow->DCTCP_FRACTION_RTT.end ())
        {
          dataSet.Add (it->first, it->second);
          it++;
        }
      if (sFlow->DCTCP_FRACTION_RTT.size() > 0)
        rttGraph.AddDataset (dataSet);
    }
  gnu.AddPlot (rttGraph);
}

/*
 * Segments are stored in this buffer based on mptcp connection sequence number.
 * So if a sub-flow's segment get delayed then other subflow's segments would be stored in-order here (subflow level).
 * This function returns false only when incoming packet is already stored before!
 */
bool
MpTcpSocketBase::StoreUnOrderedData (DSNMapping *toStore)
{
  NS_LOG_FUNCTION (this);
  for (list<DSNMapping *>::iterator it = unOrdered.begin (); it != unOrdered.end (); ++it)
    {
      DSNMapping *stored = *it;
      if (toStore->dataSeqNumber == stored->dataSeqNumber)
        {
          return false;
        }
      else if (toStore->dataSeqNumber < stored->dataSeqNumber)
        {
          // This assertion is to make sure un-ordered segments are stored in-order of subflow & connection level.
          if (toStore->subflowIndex == stored->subflowIndex)
            NS_ASSERT(toStore->subflowSeqNumber < stored->subflowSeqNumber);

          unOrdered.insert (it, toStore);
          return true;
        }
    }
  unOrdered.insert (unOrdered.end (), toStore);
  return true;
}

/** Peer sent me a FIN. Remember its sequence in rx buffer. */
void
MpTcpSocketBase::PeerClose (uint8_t sFlowIdx, Ptr<Packet> p, const TcpHeader& mptcpHeader)
{
  NS_LOG_FUNCTION (this << mptcpHeader);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // Ignore all out of range packets
  if (mptcpHeader.GetSequenceNumber () < SequenceNumber32 (sFlow->RxSeqNumber)
      || (sFlow->m_gotFin && mptcpHeader.GetSequenceNumber () < sFlow->m_finSeq))
    {
      // Note: If FIN already received then its seqNb would not be remember again!
      NS_LOG_INFO("RxSeqNumber:" << sFlow->RxSeqNumber << " ReceivedSeq:" << mptcpHeader.GetSequenceNumber().GetValue());NS_LOG_INFO("PeerClose() -> Out of range packet, ignore it " << (mptcpHeader.GetSequenceNumber().GetValue() < sFlow->RxSeqNumber) << " OR " << (mptcpHeader.GetSequenceNumber() < sFlow->m_finSeq));
      NS_FATAL_ERROR("Out of range packets received! This might occurs when FIN/ACK get lost ... Don't Panic!");
      return;
    }

  // For any case, remember the FIN position in rx buffer first
  sFlow->SetFinSequence (mptcpHeader.GetSequenceNumber () + SequenceNumber32 (p->GetSize ()));
  NS_LOG_INFO ("(" << (int)sFlow->routeId<< ") Accepted FIN at seq " << mptcpHeader.GetSequenceNumber () + SequenceNumber32 (p->GetSize ()) << ", PktSize: " << p->GetSize() << " {PeerClose}"); //
  NS_LOG_INFO ("(" << (int)sFlow->routeId<< ") RxSeqNumber: " << sFlow->RxSeqNumber<< " {PeerClose}");

  if (p->GetSize ())
    {
      NS_LOG_INFO("(" << (int)sFlowIdx<< ") FIN received with data piggyback, pkt's SeqNb: " << mptcpHeader.GetSequenceNumber () << " pktSize: " << p->GetSize());
      ReceivedData (sFlowIdx, p, mptcpHeader);
    }

  // Return if FIN is out of sequence, otherwise move to CLOSE_WAIT state by DoPeerClose
  if (!sFlow->Finished ())
    {
      NS_LOG_INFO("(" << (int)sFlowIdx << ") Return since FIN is out of sequence, its seqNb: " << sFlow->m_finSeq.GetValue() << " RxSeqNb: " << sFlow->RxSeqNumber);
      return;
    }

  // Simultaneous close: Application invoked Close() when we are processing this FIN packet
  if (sFlow->state == FIN_WAIT_1)
    {  // This is not expecting this to happens... as our implementation is not bidirectional.
      NS_LOG_INFO ("("<< (int) sFlow->routeId << ") FIN_WAIT_1 -> CLOSING {Simultaneous close}");
      sFlow->state = CLOSING;
      NS_FATAL_ERROR("Simultaneous close... it's not expected...");
      return;
    }

  DoPeerClose (sFlowIdx); // Change state, respond with ACK
}

/** Received a in-sequence FIN. Close down this socket. */
void
MpTcpSocketBase::DoPeerClose (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION((int)sFlowIdx);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  NS_ASSERT(sFlow->state == ESTABLISHED || sFlow->state == SYN_RCVD);
  /*
   * Receiver gets in-sequence FIN packet from sender.
   * It sends ACK for it and also send its own FIN at the same time since our implementation is unidirectional
   * i.e., receiver is not suppose to send data packet to sender and its sendingBuffer is uninitialized!
   */
  // Move the state of subflow to CLOSE_WAIT
  NS_LOG_INFO ("(" << (int) sFlow->routeId << ") "<< TcpStateName[sFlow->state] << " -> CLOSE_WAIT {DoPeerClose}");
  sFlow->state = CLOSE_WAIT;
  Close (sFlowIdx); // This would cause simultaneous close since receiver also want to close when she got FIN.

  if (sFlow->state == LAST_ACK)
    {
      NS_LOG_LOGIC ("MpTcpSocketBase " << this << " scheduling LATO1");
      sFlow->m_lastAckEvent = Simulator::Schedule (sFlow->rtt->RetransmitTimeout (), &MpTcpSocketBase::LastAckTimeout, this,
                                                   sFlowIdx);
    }


}

void
MpTcpSocketBase::LastAckTimeout (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  sFlow->m_lastAckEvent.Cancel ();
  if (sFlow->state == LAST_ACK)
    {
      NS_LOG_INFO("(" << (int) sFlow->routeId << ") LAST_ACK -> CLOSED {LastAckTimeout}");
      CloseAndNotify (sFlowIdx);
    }
}

bool
MpTcpSocketBase::FindPacketFromUnOrdered (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION((int)sFlowIdx);
  bool reValue = false;
  list<DSNMapping *>::iterator current = unOrdered.begin ();
  while (current != unOrdered.end ())
    {
      DSNMapping* ptrDSN = *current;
      if (ptrDSN->subflowIndex == sFlowIdx)
        {
          reValue = true;
          NS_LOG_LOGIC("(" << (int)sFlowIdx << ") FindPacketFromUnOrdered -> SeqNb" << ptrDSN->subflowSeqNumber << " pSize: " << ptrDSN->dataLevelLength);
          break;
        }
      current++;
    }
  return reValue;
}

/** This function closes the endpoint completely. Called upon RST_TX action. */
void
MpTcpSocketBase::SendRST (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this << (int) sFlowIdx); //
  //cout << Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "]{"<< flowId <<"} SendRST -> " << this << " ("<< (int) sFlowIdx << ")"<< endl;
  SendEmptyPacket (sFlowIdx, TcpHeader::RST);
  NotifyErrorClose ();
  DeallocateEndPoint (sFlowIdx);
}

void
MpTcpSocketBase::CloseAndNotifyAllSubflows ()
{
  NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " [" << m_node->GetId() << "] CloseAndNotifyAllSubflows -> subflowSize: " << subflows.size());
  // Change state of all subflow to CLOSED then call to CloseAndNotify(sFlowIdx)
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      //subflows[i]->state = CLOSED;
      CloseAndNotify (subflows[i]->routeId);
    }
}

void
MpTcpSocketBase::CloseAndNotify (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this << (int) sFlowIdx);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
//  if (!m_closeNotified)
//    {
//      NotifyNormalClose();
//    }
  if (sFlow->state != TIME_WAIT)
    {
      NS_LOG_INFO("("<< (int)sFlowIdx << ") CloseAndNotify -> DeallocateEndPoint()");
      DeallocateEndPoint (sFlowIdx);
    }NS_LOG_INFO("("<< (int)sFlowIdx << ") CloseAndNotify -> CancelAllTimers() and change the state");
  //m_closeNotified = true;
  CancelAllTimers (sFlowIdx);
  NS_LOG_INFO("("<< (int)sFlowIdx << ") "<< TcpStateName[sFlow->state] << " -> CLOSED {CloseAndNotify}");
  sFlow->state = CLOSED; // Can we remove closed subflow from subflow container????
  CloseMultipathConnection ();
}

/** Inherit from Socket class: Kill this socket and signal the peer (if any) */
int
MpTcpSocketBase::Close (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this << (int)sFlowIdx);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  // First we check to see if there is any unread rx data. Bug number 426 claims we should send reset in this case.
  if (unOrdered.size () > 0 && FindPacketFromUnOrdered (sFlowIdx) && !sFlow->Finished ()) /* && recvingBuffer->PendingData() != 0 */
    {  // I don't expect this to happens in normal scenarios!
      NS_ASSERT(server);
      //NS_FATAL_ERROR("Receiver called close() when there are some unread packets in its buffer");
      //SendRST(sFlowIdx); //?
      //CloseAndNotify(sFlowIdx);
      NS_LOG_UNCOND("unOrderedBuffer: " << unOrdered.size() << " currentSubflow: " << sFlow->routeId);
      CancelAllSubflowTimers (); // Danger?!?!
      return 0;
    }
  if (sendingBuffer.PendingData () > 0) //if (m_txBuffer.SizeFromSequence(m_nextTxSequence) > 0)
    { // App close with pending data must wait until all data transmitted from socket buffer
      NS_ASSERT(client);
      if (m_closeOnEmpty == false)
        {
          m_closeOnEmpty = true;
          if (flowType.compare ("Large") == 0 && m_backgroundFlow)
            { // This is only true for background flows
              cout << "[" << m_node->GetId () << "]{" << flowId << "}(" << flowType << ") -> DoGenerateOutPutFile()" << endl;
              flowCompletionTime = false;
              DoGenerateOutPutFile ();
              GeneratePlots ();
              NotifyFlowCompleted();
            } NS_LOG_INFO ("Socket " << this << " deferring close, Connection state " << TcpStateName[m_state] << " PendingData: " << sendingBuffer.PendingData());
        }
      return 0;
    }
//  else if (client && sendingBuffer->PendingData() == 0 && sFlow->maxSeqNb != sFlow->TxSeqNumber -1)
//    return 0;

  if (client)
    NS_ASSERT(sendingBuffer.Empty ());
  if (server && !sFlow->Finished ())
    {
      return 0;
    }
  if (server)
    NS_ASSERT_MSG(
        sFlow->Finished (),
        " state: " << TcpStateName[sFlow->state] << " GotFin: " << sFlow->m_gotFin << " FinSeq: " << sFlow->m_finSeq << " mapDSN: " << sFlow->mapDSN.size());

  return DoClose (sFlowIdx);
}

/** Do the action to close the socket. Usually send a packet with appropriate
 flags depended on the current m_state. */
int
MpTcpSocketBase::DoClose (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION (this << (int)sFlowIdx << subflows.size());

  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  //NS_LOG_INFO("DoClose -> Socket src/des (" << sFlow->sAddr << ":" << sFlow->sPort << "/" << sFlow->dAddr << ":" << sFlow->dPort << ")" << " state: " << TcpStateName[sFlow->state]);
  switch (sFlow->state)
    {
  case SYN_RCVD:
  case ESTABLISHED:
// send FIN to close the peer
    SendEmptyPacket (sFlowIdx, TcpHeader::FIN);
    NS_LOG_INFO ("("<< (int) sFlow->routeId<< ") ESTABLISHED -> FIN_WAIT_1 {DoClose} FIN is sent as separate pkt");
    sFlow->state = FIN_WAIT_1;
    break;
  case CLOSE_WAIT:
// send FIN+ACK to close the peer (in normal scenario receiver should use this when she got FIN from sender)
    SendEmptyPacket (sFlowIdx, TcpHeader::FIN | TcpHeader::ACK);
    NS_LOG_INFO ("("<< (int) sFlow->routeId<< ") CLOSE_WAIT -> LAST_ACK {DoClose}");
    sFlow->state = LAST_ACK;
    break;
  case SYN_SENT:
  case CLOSING:
// Send RST if application closes in SYN_SENT and CLOSING
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " ["<<m_node->GetId() << "] DoClose (SYN_SENT or CLOSING)-> Socket src/des (" << sFlow->sAddr << ":" << sFlow->sPort << "/" << sFlow->dAddr << ":" << sFlow->dPort << ")" << " sFlow->state: " << TcpStateName[sFlow->state]);
    //CancelAllSubflowTimers(); // Danger?!?!
    //sFlow->state = CLOSED;
    cout << Simulator::Now ().GetSeconds () << " [" << m_node->GetId () << "](" << (int) sFlowIdx << "){" << flowId << "}("
        << flowType << ") " << TcpStateName[sFlow->state] << " <-- SendRST(DoCLOSE)" << endl;
    SendRST (sFlowIdx);
    CloseAndNotifyAllSubflows ();
    break;
  case LISTEN:
  case LAST_ACK:
// In these three states, move to CLOSED and tear down the end point
    CloseAndNotify (sFlowIdx);
    break;
  case CLOSED:
  case FIN_WAIT_1:
  case FIN_WAIT_2:
  case TIME_WAIT:
  default: /* mute compiler */
//NS_LOG_INFO("DoClose -> DoNotting since subflow's state is " << TcpStateName[sFlow->state] << "(" << sFlow->routeId<< ")");
// Do nothing in these four states
    break;
    }
  return 0;
}

int
MpTcpSocketBase::Close (void)
{
  NS_LOG_FUNCTION(this);
  if (subflows.size () > 0)
    {
        { // This block could be removed...
          if (subflows.size () == 1)
            NS_ASSERT(currentSublow == 0); NS_LOG_WARN("Close() -> CurrentSubflow: " << (int)currentSublow);
        } //-------------------------------
      return Close (currentSublow);
    }
  else
    { //CloseMultipathConnection(); this could be used as well...
      NS_LOG_INFO("Close has issued for listening socket, "<< this <<", it's endpoints ->  local/remote (" << m_endPoint->GetLocalAddress() << ":" << m_endPoint->GetLocalPort() << "/" << m_endPoint->GetPeerAddress() << ":" << m_endPoint->GetPeerPort() << ") m_state: " << TcpStateName[m_state] << " -> CLOSED");
      NS_ASSERT(subflows.size () == 0);
      m_state = CLOSED;
      NotifyNormalClose ();
      m_endPoint->SetDestroyCallback (MakeNullCallback<void> ());
      m_tcp->DeAllocate (m_endPoint);
      m_endPoint = 0;
      m_tcp->RemoveLocalToken (localToken);
      std::vector<Ptr<TcpSocketBase> >::iterator it = std::find (m_tcp->m_sockets.begin (), m_tcp->m_sockets.end (), this);
      if (it != m_tcp->m_sockets.end ())
        {
          m_tcp->m_sockets.erase (it);
        }
      CancelAllSubflowTimers ();

    }
  return true;
}

// This function would calls NotifyNormalClose() where in turn calls mpTopology:HandlePeerClose where in turn calls close();
bool
MpTcpSocketBase::CloseMultipathConnection ()
{
  NS_LOG_FUNCTION_NOARGS();
  bool closed = false;
  uint32_t cpt = 0;
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      NS_LOG_LOGIC("Subflow (" << i << ") TxSeqNb (" << subflows[i]->TxSeqNumber << ") RxSeqNb = " << subflows[i]->RxSeqNumber << " highestAck (" << subflows[i]->highestAck << ") maxSeqNb (" << subflows[i]->maxSeqNb << ")");

      if (subflows[i]->state == CLOSED)
        cpt++;
      if (subflows[i]->state == TIME_WAIT)
        {
          NS_LOG_INFO("("<< (int)subflows[i]->routeId<< ") "<< TcpStateName[subflows[i]->state] << " -> CLOSED {CloseMultipathConnection}");
          subflows[i]->state = CLOSED;
          cpt++;
        }
    }
  if (cpt == subflows.size ())
    {
      if (m_state == ESTABLISHED && client) // We could remove client ... it should work but it generate plots for receiver as well.
        {
          NS_LOG_INFO("CloseMultipathConnection -> GENERATE PLOTS SUCCESSFULLY -> HoOoOrA  pAck: " << pAck);
//          GenerateCWNDPlot();
//          GenerateSendvsACK();
//          GeneratePlots();
        }
      if (m_state != CLOSED)
        {
          NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "["<< m_node->GetId() << "] CloseMultipathConnection -> MPTCP connection is closed {" << this << "}, m_state: " << TcpStateName[m_state] << " -> CLOSED" << " CurrentSubflow (" << (int)currentSublow << ") SubflowsSize: " <<subflows.size());
          m_state = CLOSED;
          NotifyNormalClose ();
          m_endPoint->SetDestroyCallback (MakeNullCallback<void> ()); // Remove callback to destroy()
          m_tcp->DeAllocate (m_endPoint);  // Deallocating the endPoint
          m_endPoint = 0;
          if (subflows.size () > 0)
            subflows[0]->m_endPoint = 0;
          m_tcp->RemoveLocalToken (localToken);
          std::vector<Ptr<TcpSocketBase> >::iterator it = std::find (m_tcp->m_sockets.begin (), m_tcp->m_sockets.end (), this);
          if (it != m_tcp->m_sockets.end ())
            {
              m_tcp->m_sockets.erase (it);
            }
          CancelAllSubflowTimers ();
        }
    }
  return closed;
}

void
MpTcpSocketBase::AdvertiseAvailableAddresses ()
{
  NS_LOG_FUNCTION(m_node->GetId());
  if (mpEnabled == true)
    {
      // there is at least one subflow
      Ptr<MpTcpSubFlow> sFlow = subflows[0];
      NS_ASSERT(sFlow != 0);

      // Change the MPTCP send state to MP_ADDR
      mpSendState = MP_ADDR;
      MpTcpAddressInfo * addrInfo;
      Ptr<Packet> pkt = Create<Packet> ();

      TcpHeader header;
      header.SetFlags (TcpHeader::ACK);
      header.SetSequenceNumber (SequenceNumber32 (sFlow->TxSeqNumber));
      header.SetAckNumber (SequenceNumber32 (sFlow->RxSeqNumber));
      header.SetSourcePort (m_localPort); // m_endPoint->GetLocalPort()
      header.SetDestinationPort (m_remotePort); // TODO Is this right?
      uint8_t hlen = 0;
      uint8_t olen = 0;

      // Object from L3 to access to routing protocol, Interfaces and NetDevices and so on.
      Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();
      for (uint32_t i = 0; i < ipv4->GetNInterfaces (); i++)
        {
          //Ptr<NetDevice> device = m_node->GetDevice(i);
          Ptr<Ipv4Interface> interface = ipv4->GetInterface (i);
          Ipv4InterfaceAddress interfaceAddr = interface->GetAddress (0);

          // Skip the loop-back
          if (interfaceAddr.GetLocal () == Ipv4Address::GetLoopback ())
            continue;

          addrInfo = new MpTcpAddressInfo ();
          addrInfo->addrID = i;
          addrInfo->ipv4Addr = interfaceAddr.GetLocal ();
          addrInfo->mask = interfaceAddr.GetMask ();
          header.AddOptADDR (OPT_ADDR, addrInfo->addrID, addrInfo->ipv4Addr);
          olen += 6;
          localAddrs.insert (localAddrs.end (), addrInfo);
        }
      uint8_t plen = (4 - (olen % 4)) % 4;
      header.SetWindowSize (AdvertisedWindowSize ());
      olen = (olen + plen) / 4;
      hlen = 5 + olen;
      header.SetLength (hlen);
      header.SetOptionsLength (olen);
      header.SetPaddingLength (plen);

      // @AdvertiseAvailableAddresses -> Add Control packet
      AddPacketTag(pkt, CP_TAG);

      //m_tcp->SendPacket(pkt, header, m_endPoint->GetLocalAddress(), m_remoteAddress);
      m_tcp->SendPacket (pkt, header, m_localAddress, m_remoteAddress, FindOutputNetDevice (m_localAddress));
      NS_LOG_INFO("AdvertiseAvailableAddresses-> "<< header);
    }
  else
    {
      NS_FATAL_ERROR("Need to be Looked...");
    }
}

bool
MpTcpSocketBase::IsThereRoute (Ipv4Address src, Ipv4Address dst)
{
  NS_LOG_FUNCTION(this << src << dst);
  bool found = false;
  // Look up the source address
//  Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
  Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();
  if (ipv4->GetRoutingProtocol () != 0)
    {
      Ipv4Header l3Header;
      Socket::SocketErrno errno_;
      Ptr<Ipv4Route> route;
      //.....................................................................................
      //NS_LOG_INFO("----------------------------------------------------");NS_LOG_INFO("IsThereRoute() -> src: " << src << " dst: " << dst);
      // Get interface number from IPv4Address via ns3::Ipv4::GetInterfaceForAddress(Ipv4Address address);
      int32_t interface = ipv4->GetInterfaceForAddress (src);        // Morteza uses sign integers
      Ptr<Ipv4Interface> v4Interface = ipv4->GetRealInterfaceForAddress (src);
      Ptr<NetDevice> v4NetDevice = v4Interface->GetDevice ();
      //PrintIpv4AddressFromIpv4Interface(v4Interface, interface);
      NS_ASSERT_MSG(interface != -1, "There is no interface object for the the src address");
      // Get NetDevice from Interface via ns3::Ipv4::GetNetDevice(uint32_t interface);
      Ptr<NetDevice> oif = ipv4->GetNetDevice (interface);
      NS_ASSERT(oif == v4NetDevice);

      //.....................................................................................
      l3Header.SetSource (src);
      l3Header.SetDestination (dst);
      route = ipv4->GetRoutingProtocol ()->RouteOutput (Ptr<Packet> (), l3Header, oif, errno_);
      if ((route != 0)/* && (src == route->GetSource())*/)
        {
          NS_LOG_DEBUG ("IsThereRoute -> Route from src "<< src << " to dst " << dst << " oit ["<< oif->GetIfIndex()<<"], exist  Gateway: " << route->GetGateway());
          found = true;
        }
      else
        {
          NS_LOG_DEBUG ("IsThereRoute -> No Route from srcAddr "<< src << " to dstAddr " << dst << " oit ["<<oif->GetIfIndex()<<"], exist Gateway: " << route->GetGateway());
        }
    }
  return found;
}

void
MpTcpSocketBase::PrintIpv4AddressFromIpv4Interface (Ptr<Ipv4Interface> interface, int32_t indexOfInterface)
{
  NS_LOG_FUNCTION_NOARGS();

  for (uint32_t i = 0; i < interface->GetNAddresses (); i++)
    {
      NS_LOG_INFO("Node(" << interface->GetDevice()->GetNode()->GetId() << ") Interface(" << indexOfInterface << ") Ipv4Index(" << i << ")" << " Ipv4Address(" << interface->GetAddress(i).GetLocal()<< ")");
    }
}

Ptr<NetDevice>
MpTcpSocketBase::FindOutputNetDevice (Ipv4Address src)
{

  Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();
  uint32_t oInterface = ipv4->GetInterfaceForAddress (src);
  Ptr<NetDevice> oNetDevice = ipv4->GetNetDevice (oInterface);

 return oNetDevice;
}

bool
MpTcpSocketBase::IsLocalAddress (Ipv4Address addr)
{
  NS_LOG_FUNCTION(this << addr);
  bool found = false;
  MpTcpAddressInfo * pAddrInfo;
  for (uint32_t i = 0; i < localAddrs.size (); i++)
    {
      pAddrInfo = localAddrs[i];
      if (pAddrInfo->ipv4Addr == addr)
        {
          found = true;
          break;
        }
    }
  return found;
}

bool
MpTcpSocketBase::IsRemoteAddress (Ipv4Address addr)
{
  bool found = false;
  MpTcpAddressInfo * mpAddrInfo;
  for (uint32_t i = 0; i < remoteAddrs.size (); i++)
    {
      mpAddrInfo = remoteAddrs[i];
      if (mpAddrInfo->ipv4Addr == addr)
        {
          found = true;
          break;
        }
    }
  return found;
}

uint32_t
MpTcpSocketBase::BytesInFlight (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION(this);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  //return sFlow->maxSeqNb - sFlow->highestAck;        //m_highTxMark - m_highestRxAck;
  return sFlow->m_highTxMark - sFlow->highestAck;
}

uint16_t
MpTcpSocketBase::AdvertisedWindowSize ()
{

  return (uint16_t) 65535;
}

uint32_t
MpTcpSocketBase::AvailableWindow (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION(this << (int)sFlowIdx);

  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint32_t window = std::min ((m_rwndScale*remoteRecvWnd), sFlow->cwnd.Get ());
  uint32_t unAcked = (sFlow->TxSeqNumber - (sFlow->highestAck + 1));
  uint32_t freeCWND = (window < unAcked) ? 0 : (window - unAcked);
  if (freeCWND < sFlow->MSS && sendingBuffer.PendingData () >= sFlow->MSS)
    {
      NS_LOG_WARN("AvailableWindow: ("<< (int)sFlowIdx <<") -> " << freeCWND << " => 0" << " MSS: " << sFlow->MSS);
      return 0;
    }
  else
    {
      NS_LOG_WARN("AvailableWindow: ("<< (int)sFlowIdx <<") -> " << freeCWND );
      return freeCWND;
    }
}

uint32_t
MpTcpSocketBase::GetTxAvailable ()
{
  NS_LOG_FUNCTION_NOARGS();
  return sendingBuffer.FreeSpaceSize ();
}

void
MpTcpSocketBase::SetSourceAddress (Ipv4Address src)
{
  NS_LOG_FUNCTION_NOARGS();
  m_localAddress = src;
  if (m_endPoint != 0)
    {
      m_endPoint->SetLocalAddress (src);
    }
}

Ipv4Address
MpTcpSocketBase::GetSourceAddress ()
{
  NS_LOG_FUNCTION_NOARGS();
  return m_localAddress;
}
int
MpTcpSocketBase::LookupSubflow (Ipv4Address src, uint32_t srcPort, Ipv4Address dst, uint32_t dstPort)
{
  NS_LOG_FUNCTION(this);

  NS_ASSERT(m_localAddress == src);
  NS_ASSERT(m_remoteAddress == dst);
  NS_ASSERT(m_localPort == srcPort);
  NS_ASSERT(m_remotePort == dstPort);

  Ptr<MpTcpSubFlow> sFlow = 0;
  uint8_t sFlowIdx = maxSubflows;

  // Walk through the existing subflow container and try to find one with 4-tuple match!
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      sFlow = subflows[i];
      if (sFlow->sAddr == src && sFlow->dAddr == dst && sFlow->sPort == srcPort && sFlow->dPort == dstPort)
        {
          sFlowIdx = i;
          return sFlowIdx;
        }
    }

  // For now this should be happen only at server side
  NS_ASSERT(server);

  // If a new subflow need to be created then src/dst pair should be already known to mptcp socket.
//  if (!IsLocalAddress(src) || !IsRemoteAddress(dst))
//    return -1;

// Recevier would create its new subflow when SYN with MP_JOIN being sent.
  sFlowIdx = subflows.size ();
  sFlow = CreateObject<MpTcpSubFlow> ();
  sFlow->routeId = subflows[subflows.size () - 1]->routeId + 1;
  sFlow->dAddr = dst;
  sFlow->dPort = dstPort;
  sFlow->sAddr = src;
  sFlow->sPort = srcPort;
  sFlow->MSS = segmentSize;
  sFlow->cwnd = sFlow->MSS;
  sFlow->state = LISTEN;
  sFlow->cnTimeout = m_cnTimeout;
  sFlow->cnRetries = m_cnRetries;
  sFlow->cnCount = sFlow->cnRetries;
  sFlow->rtt->SetG (m_g);
  sFlow->m_endPoint = m_tcp->Allocate (sFlow->sAddr, sFlow->sPort, sFlow->dAddr, sFlow->dPort);
  if (sFlow->m_endPoint == 0)
    return -1;
  sFlow->m_endPoint->SetRxCallback (MakeCallback (&MpTcpSocketBase::ForwardUp, Ptr<MpTcpSocketBase> (this)));
  subflows.insert (subflows.end (), sFlow);
  NS_LOG_UNCOND(this << " LookupSubflow -> Subflow(" << (int) sFlowIdx <<") has created its (src,dst) = (" << sFlow->sAddr << ":" << sFlow->sPort << " , "<< sFlow->dAddr << ":" << sFlow->dPort<< ")" );

  return sFlowIdx;
}

uint32_t
MpTcpSocketBase::compute_a_scaled ()
{
  if (AlgoCC < COUPLED_EPSILON)
    exit (1);

  uint32_t sum_denominator = 0;
  uint64_t t = 0;
  uint64_t cwndSum = 0;

  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[i];
      Time time = sFlow->rtt->GetCurrentEstimate ();
      uint32_t rtt = time.GetMicroSeconds () / 10;
      if (rtt == 0)
        rtt = 1;

      uint32_t cwnd = sFlow->m_inFastRec ? sFlow->ssthresh : sFlow->cwnd.Get ();
      uint32_t mss = sFlow->MSS;

      t = max (t, (uint64_t) cwnd * mss * mss / rtt / rtt);
      sum_denominator += cwnd * mss / rtt;
      cwndSum += cwnd;
    }
  return (uint32_t) (A_SCALE * (uint64_t) cwndSum * t / sum_denominator / sum_denominator);
}

double
MpTcpSocketBase::compute_alfa ()
{
  if (AlgoCC < COUPLED_EPSILON)
    exit (300);

  if (subflows.size () == 1)
    {
      return 1;
    }
  else
    {
      double maxt = 0, sum_denominator = 0;
      for (uint32_t i = 0; i < subflows.size (); i++)
        {
          Ptr<MpTcpSubFlow> sFlow = subflows[i];
          uint32_t cwnd = sFlow->m_inFastRec ? sFlow->ssthresh : sFlow->cwnd.Get ();
          Time time = sFlow->rtt->GetCurrentEstimate ();
          uint32_t rtt = time.GetMilliSeconds ();

          if (rtt == 0)
            rtt = 1;

          double t = pow (cwnd, _e / 2) / rtt;
          if (t > maxt)
            maxt = t;
          sum_denominator += ((double) cwnd / rtt);
        }
      return (double) compute_total_window () * pow (maxt, 1 / (1 - _e / 2)) / pow (sum_denominator, 1 / (1 - _e / 2));
    }
}

void
MpTcpSocketBase::window_changed ()
{
  switch (AlgoCC)
    {
  case COUPLED_EPSILON:
    if (_e > 0 && _e < 2)
      alpha = compute_alfa ();
    return;
  case COUPLED_INC:
    a = compute_a_scaled ();
    return;
  default:
    break;
    }
}

void
MpTcpSocketBase::OpenCWND (uint8_t sFlowIdx, uint32_t ackedBytes)
{
  NS_LOG_FUNCTION(this << (int) sFlowIdx << ackedBytes);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  double adder = 0;
  uint32_t cwnd = sFlow->cwnd.Get ();
  uint32_t ssthresh = sFlow->ssthresh;
  uint32_t mss = sFlow->MSS;

  // params used only for COUPLED_INC and COUPLED_EPS only
  int tcp_inc = 0, tt = 0;
  int tmp, total_cwnd, tmp2;
  double tmp_float;

  if (AlgoCC >= COUPLED_SCALABLE_TCP)
    {
      if (ackedBytes > mss)
        ackedBytes = mss;
      if (ackedBytes < 0)
        {
          exit (200);
          return;
        }
      tcp_inc = (ackedBytes * mss) / cwnd;
      tt = (ackedBytes * mss) % cwnd;
      if (m_alphaPerAck)
        {
          a = compute_a_scaled (); // Per ACK for COUPLED_INC
          alpha = compute_alfa (); // Per ACK for COUPLED_EPSILON
        }
    }

  calculateTotalCWND ();
  if (cwnd < ssthresh)
    {
      sFlow->cwnd += sFlow->MSS;
#ifdef PLOT
      sFlow->ssthreshtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->ssthresh));
      sFlow->CWNDtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->cwnd));
      totalCWNDtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), totalCwnd));
      sFlow->_ss.push_back (make_pair (Simulator::Now ().GetSeconds (), TimeScale));
#endif
      NS_LOG_WARN ("Congestion Control (Slow Start) increment by one segmentSize");
    }
  else
    {
      switch (AlgoCC)
        {
      case Fast_Uncoupled:
        //calculateFastAlpha(sFlowIdx);
        adder = ((1 - sFlow->dctcp_last_fraction) * sFlow->MSS * sFlow->MSS) / cwnd;
        adder = std::max (1.0, adder);
        sFlow->cwnd += static_cast<double> (adder);
        break;
      case Fast_Increases:
        adder = ((1 - sFlow->dctcp_last_fraction) * sFlow->MSS * sFlow->MSS) / totalCwnd;
        adder = std::max(1.0, adder);
        sFlow->cwnd += static_cast<double>(adder);
        break;
      case XCA:
        adder = static_cast<double> (sFlow->MSS * sFlow->MSS) / totalCwnd;
        adder = std::max (1.0, adder);
        sFlow->cwnd += static_cast<double> (adder);
        break;
      case RTT_Compensator:
        calculateAlpha (); // Calculate alpha per drop or RTT...RFC 6356 (Section 4.1)
        adder = std::min (alpha * sFlow->MSS * sFlow->MSS / totalCwnd, static_cast<double> (sFlow->MSS * sFlow->MSS) / cwnd);
        adder = std::max (1.0, adder);
        sFlow->cwnd += static_cast<double> (adder);
        NS_LOG_ERROR ("Congestion Control (RTT_Compensator): alpha "<<alpha<<" ackedBytes (" << ackedBytes << ") totalCwnd ("<< totalCwnd / sFlow->MSS<<" packets) -> increment is "<<adder << " cwnd: " << sFlow->cwnd);
        break;
      case Linked_Increases:
        calculateAlpha ();
        adder = alpha * sFlow->MSS * sFlow->MSS / totalCwnd;
        adder = std::max (1.0, adder);
        sFlow->cwnd += static_cast<double> (adder);
        NS_LOG_ERROR ("Subflow "<<(int)sFlowIdx<<" Congestion Control (Linked_Increases): alpha "<<alpha<<" increment is "<<adder<<" ssthresh "<< ssthresh << " cwnd "<<cwnd );
        break;
      case Uncoupled_TCPs:
        adder = static_cast<double> (sFlow->MSS * sFlow->MSS) / cwnd;
        adder = std::max (1.0, adder);
        sFlow->cwnd += static_cast<double> (adder);
        NS_LOG_WARN ("Subflow "<<(int)sFlowIdx<<" Congestion Control (Uncoupled_TCPs) increment is "<<adder<<" ssthresh "<< ssthresh << " cwnd "<<cwnd);
        break;
      case UNCOUPLED:
        sFlow->cwnd += tcp_inc;
        break;
      case Fully_Coupled:
        adder = static_cast<double> (sFlow->MSS * sFlow->MSS) / totalCwnd;
        adder = std::max (1.0, adder);
        sFlow->cwnd += static_cast<double> (adder);
        NS_LOG_ERROR ("Subflow "<<(int)sFlowIdx<<" Congestion Control (Fully_Coupled) increment is "<<adder<<" ssthresh "<< ssthresh << " cwnd "<<cwnd);
        break;
      case COUPLED_INC:
        total_cwnd = compute_total_window ();
        tmp2 = (ackedBytes * mss * a) / total_cwnd;

        tmp = tmp2 / A_SCALE;

        if (tmp < 0)
          {
            printf ("Negative increase!");
            tmp = 0;
          }

        if (rand () % A_SCALE < tmp2 % A_SCALE)
          tmp++;

        if (tmp > tcp_inc)    //capping
          tmp = tcp_inc;

        if ((cwnd + tmp) / mss != cwnd / mss)
          a = compute_a_scaled ();

        sFlow->cwnd = cwnd + tmp;
        break;
      case COUPLED_EPSILON: // RTT_Compensator
        total_cwnd = compute_total_window ();
        tmp_float = ((double) ackedBytes * mss * alpha * pow (alpha * cwnd, 1 - _e)) / pow (total_cwnd, 2 - _e);
        tmp = (int) floor (tmp_float);

        if (drand () < tmp_float - tmp)
          tmp++;

        if (tmp > tcp_inc)    //capping
          tmp = tcp_inc;

        if ((cwnd + tmp) / mss != cwnd / mss)
          {
            if (_e > 0 && _e < 2)
              alpha = compute_alfa ();
          }
        sFlow->cwnd = cwnd + tmp;
        break;
      case COUPLED_SCALABLE_TCP:
        sFlow->cwnd = cwnd + ackedBytes * 0.01;
        break;
      case COUPLED_FULLY:
        total_cwnd = compute_total_window ();
        tt = (int) (ackedBytes * mss * A);
        tmp = tt / total_cwnd;
        if (tmp > tcp_inc)
          tmp = tcp_inc;
        sFlow->cwnd = cwnd + tmp;
        break;
      default:
        break;
        }
#ifdef PLOT
      sFlow->ssthreshtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->ssthresh));
      sFlow->CWNDtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), sFlow->cwnd));
      totalCWNDtrack.push_back (make_pair (Simulator::Now ().GetSeconds (), totalCwnd));
      sFlow->_ca.push_back (make_pair (Simulator::Now ().GetSeconds (), TimeScale));
#endif
    }
}

void
MpTcpSocketBase::calculateAlpha ()
{
  // this method is called whenever a congestion happen in order to regulate the agressivety of subflows
  // alpha = cwnd_total * MAX(cwnd_i / rtt_i^2) / {SUM(cwnd_i / rtt_i))^2}   //RFC 6356 formula (2)

  NS_LOG_FUNCTION_NOARGS ();
  alpha = 0;
  double maxi = 0;
  double sumi = 0;

  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[i];
      Time time = sFlow->rtt->GetCurrentEstimate ();
      double rtt = time.GetMicroSeconds(); //double rtt = time.GetSeconds ();
      if (rtt == 0)
        rtt = 1;
      double tmpi = sFlow->cwnd.Get () / (rtt * rtt);
      if (maxi < tmpi)
        maxi = tmpi;

      sumi += sFlow->cwnd.Get () / rtt;
    }
  alpha = (totalCwnd * maxi) / (sumi * sumi);
}

void
MpTcpSocketBase::calculateFastAlpha (uint8_t sFlowIdx)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  if (sFlow->dctcp_last_fraction == 1 || sFlow->curEcnState == true)
    sFlow->fast_alpha = 0; // Do not increase cwnd when last/current window has received ECN signal
  else if (sFlow->dctcp_last_fraction == 0 && sFlow->curEcnState == false)
    sFlow->fast_alpha = 2; // Do increase cwnd by 2 MSS when last and current window are free from marked ACKs
  else
    sFlow->fast_alpha = 1; // Do increase by 1 MSS when some ACKs have been marked in the last window & but current window is clean
}

void
MpTcpSocketBase::DestroySubflowMapDSN ()
{
  NS_LOG_FUNCTION_NOARGS();
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      Ptr<MpTcpSubFlow> sFlow = subflows[i];
      for (std::list<DSNMapping *>::iterator i = sFlow->mapDSN.begin (); i != sFlow->mapDSN.end (); i++)
        {
          i = sFlow->mapDSN.erase (i);
        }
      sFlow->mapDSN.clear ();
    }
}

void
MpTcpSocketBase::DestroyUnOrdered ()
{
  NS_LOG_FUNCTION_NOARGS();
  for (std::list<DSNMapping*>::iterator i = unOrdered.begin (); i != unOrdered.end (); i++)
    {
      i = unOrdered.erase (i);
    }
  unOrdered.clear ();
}

/** Kill this socket. This is a callback function configured to m_endpoint in
 SetupCallback(), invoked when the endpoint is destroyed. */
void
MpTcpSocketBase::Destroy (void)
{
  NS_LOG_FUNCTION(this);  //
  NS_LOG_INFO("Enter Destroy(" << this << ") m_sockets:  " << m_tcp->m_sockets.size()<< ")");
  m_endPoint = 0;
  if (m_tcp != 0)
    {
      std::vector<Ptr<TcpSocketBase> >::iterator it = std::find (m_tcp->m_sockets.begin (), m_tcp->m_sockets.end (), this);
      if (it != m_tcp->m_sockets.end ())
        {
          m_tcp->m_sockets.erase (it);
        }
    }
  CancelAllSubflowTimers ();
  NS_LOG_INFO("Leave Destroy(" << this << ") m_sockets:  " << m_tcp->m_sockets.size()<< ")");
}

/** Deallocate the end point and cancel all the timers */
void
MpTcpSocketBase::DeallocateEndPoint (uint8_t sFlowIdx)
{
  NS_LOG_FUNCTION(this << (int) sFlowIdx);
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  // Master subflow would be closed when all other slave's subflows are closed.
  if (sFlowIdx == 0)
    {
      NS_LOG_INFO( "("<< (int)sFlowIdx<< ") DeallocateEndPoint -> Master Subflow want to deallocate its endpoint, call on CloseMultipathConnection()");
      CloseMultipathConnection ();
    }
  // Slave's subflows
  else
    {
      if (sFlow->m_endPoint != 0)
        {
          NS_LOG_INFO("Salve subflow ("<< (int)sFlowIdx << ") is deallocated its endpoint");
          sFlow->m_endPoint->SetDestroyCallback (MakeNullCallback<void> ());
          m_tcp->DeAllocate (sFlow->m_endPoint);
          sFlow->m_endPoint = 0;
          CancelAllTimers (sFlowIdx);
        }
    }
}


void
MpTcpSocketBase::SetCCAlgo (string cc)
{
  if (cc == "Uncoupled_TCPs")
    AlgoCC = Uncoupled_TCPs;
  else if (cc == "Linked_Increases")
    AlgoCC = Linked_Increases;
  else if (cc == "RTT_Compensator")
    AlgoCC = RTT_Compensator;
  else if (cc == "Fully_Coupled")
    AlgoCC = Fully_Coupled;
  else if (cc == "COUPLED_SCALABLE_TCP")
    AlgoCC = COUPLED_SCALABLE_TCP;
  else if (cc == "UNCOUPLED")
    AlgoCC = UNCOUPLED;
  else if (cc == "COUPLED_EPSILON")
    AlgoCC = COUPLED_EPSILON;
  else if (cc == "COUPLED_INC")
    AlgoCC = COUPLED_INC;
  else if (cc == "COUPLED_FULLY")
    AlgoCC = COUPLED_FULLY;
  else if (cc == "Fast_Uncoupled")
    AlgoCC = Fast_Uncoupled;
  else if (cc == "Fast_Increases")
    AlgoCC = Fast_Increases;
  else if (cc == "XCA")
    AlgoCC = XCA;
}

void
MpTcpSocketBase::SetCongestionCtrlAlgo (CongestionCtrl_t ccalgo)
{
  AlgoCC = ccalgo;
}

void
MpTcpSocketBase::SetDataDistribAlgo (DataDistribAlgo_t ddalgo)
{
  distribAlgo = ddalgo;
}

void
MpTcpSocketBase::SetPathManager (PathManager_t pManagerMode)
{
  pathManager = pManagerMode;
}

void
MpTcpSocketBase::SetFlowSize(uint32_t size)
{
  flowSize = size;
}

void
MpTcpSocketBase::SetFlowId (uint32_t fd)
{
  flowId = fd;
}

void
MpTcpSocketBase::SetFlowType (string input)
{
  flowType = input;
}

void
MpTcpSocketBase::SetFlowLayer (string input)
{
  flowLayer = input;
}

void
MpTcpSocketBase::SetSocketModel (string input)
{
  socketModel = input;
}

void
MpTcpSocketBase::SetOutputFileName (string input)
{
  outputFileName = input;
}

void
MpTcpSocketBase::SetOutputFileNameDctcp (string input)
{
  outputFileNameDctcp = input;
}
//DSNMapping*
//MpTcpSocketBase::getAckedSegment(uint64_t lEdge, uint64_t rEdge)
//{
//  for (uint8_t i = 0; i < subflows.size(); i++)
//    {
//      Ptr<MpTcpSubFlow> sFlow = subflows[i];
//      for (list<DSNMapping *>::iterator it = sFlow->mapDSN.begin(); it != sFlow->mapDSN.end(); ++it)
//        {
//          DSNMapping* dsn = *it;
//          if (dsn->dataSeqNumber == lEdge && dsn->dataSeqNumber + dsn->dataLevelLength == rEdge)
//            {
//              return dsn;
//            }
//        }
//    }
//  return 0;
//}
// ...........................................................................
// Extra Functions for evaluation and plotting pusposes only
// ...........................................................................
void
MpTcpSocketBase::getQueuePkt (Ipv4Address addr)
{
  Ptr<Ipv4L3Protocol> l3Protocol = m_node->GetObject<Ipv4L3Protocol> ();
  Ptr<Ipv4Interface> ipv4If = l3Protocol->GetInterface (l3Protocol->GetInterfaceForAddress (addr));
  Ptr<NetDevice> net0 = ipv4If->GetDevice ();
  PointerValue ptr;
  net0->GetAttribute ("TxQueue", ptr);
  Ptr<Queue> txQueue = ptr.Get<Queue> ();
  TxQueue.push_back (make_pair (Simulator::Now ().GetSeconds (), txQueue->GetNPackets ()));
}

string
MpTcpSocketBase::GenerateEpsFileName ()
{
  stringstream oss;
  std::string::size_type pos = std::string::npos;
  string fileName = CutFileName (pos);
  if (pos != string::npos)
    {
      oss << "_PLOT_" << m_node->GetId () << "_" << flowId << "_" << PrintCC (AlgoCC) << "_";
      fileName.insert (pos, oss.str ());
    }
  else
    {
      oss << "_PLOT_" << m_node->GetId () << "_" << flowId << "_" << PrintCC (AlgoCC);
      fileName += oss.str ();
    }
  oss.str("");
  return fileName;
}


// PLOT_CI_5_8_MPTCP_0_1
void
MpTcpSocketBase::GeneratePlotsOutput ()
{
  stringstream oss;
  //oss << "PLOT_" << m_node->GetId() << "_" << flowId << "_" << GetTypeIdName() <<"_"<< PrintCC(AlgoCC) <<"_"<< (int)maxSubflows << "_" << (uint32_t)Simulator::Now().GetSeconds();
  std::string::size_type pos = std::string::npos;
  string fileName = CutFileName (pos);
  if (pos != string::npos)
    {
      oss << "_PLOT_" << m_node->GetId () << "_" << flowId << "_" << PrintCC (AlgoCC) << "_";
      fileName.insert (pos, oss.str ());
      fileName += ".data";
      oss.str("");
    }
  else
    {
      oss << "_PLOT_" << m_node->GetId () << "_" << flowId << "_" << PrintCC (AlgoCC) << ".data";
      fileName += oss.str ();
      oss.str("");
    }
  /*
   oss << "PLOT_" << PrintCC (AlgoCC) << "_" << (uint32_t) Simulator::Now ().GetSeconds () << "_" << (int) maxSubflows << "_"
   << GetTypeIdName () << "_" << m_node->GetId () << "_" << flowId << "_" << m_rGap;
   string tmp = oss.str ();
   oss.str ("");
   */
  //std::ofstream outfile(oss.str().c_str());
  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper> (fileName.c_str (), std::ios::out);
  ostream* os = stream->GetStream ();
  gnu.GenerateOutput (*os);
//  oss.str("");
//  outfile.close();
}

uint16_t
MpTcpSocketBase::GetRandom16 ()
{
  Ptr<UniformRandomVariable> uniRandom = CreateObject<UniformRandomVariable> ();
  return uniRandom->GetInteger (1, 65535);
}

uint32_t
MpTcpSocketBase::GetRandom32 ()
{
  Ptr<UniformRandomVariable> uniRandom = CreateObject<UniformRandomVariable> ();
  return uniRandom->GetInteger (1, 4294967295);
}

uint32_t
MpTcpSocketBase::GetRandom (uint32_t min, uint32_t max)
{
  Ptr<UniformRandomVariable> uniRandom = CreateObject<UniformRandomVariable> ();
  return uniRandom->GetInteger (min, max);
}

//NONE = 0, FIN = 1, SYN = 2, RST = 4, PSH = 8, ACK = 16, URG = 32, ECE = 64, CWR = 128
string
MpTcpSocketBase::TcpFlagPrinter (uint8_t flag)
{
  ostringstream oss;
  oss << "[";
  if (flag & TcpHeader::SYN)
    oss << " SYN ";
  if (flag & TcpHeader::FIN)
    oss << " FIN ";
  if (flag & TcpHeader::ACK)
    oss << " ACK ";
  if (flag & TcpHeader::RST)
    oss << " RST ";
  if (flag & TcpHeader::NONE)
    oss << " NONE";
  oss << "]";
  string tmp = oss.str ();
  oss.str ("");
  return tmp;
}

string
MpTcpSocketBase::IsActive (uint32_t boolean)
{
  if (boolean == 0)
    return "Inactive";
  else
    return "Active";
}

void
MpTcpSocketBase::SetDupAckThresh (uint32_t)
{
  NS_LOG_FUNCTION_NOARGS();
}

double
MpTcpSocketBase::drand ()
{
  int r = rand ();
  int m = RAND_MAX;
  double d = (double) r / (double) m;
  return d;
}

string
MpTcpSocketBase::GetTypeIdName ()
{
  string tmp = this->GetTypeId ().GetName ();
  if (tmp.compare ("ns3::MpTcpSocketBase") == 0)
    return "MPTCP";
  else
    return "UnKnown";
}

void
MpTcpSocketBase::GeneratePlots ()
{
  if ((m_largePlotting && (flowType.compare ("Large") == 0)) || (m_shortPlotting && (flowType.compare ("Short") == 0)))
    {
      GenerateCwndTracer ();
      GenerateRTT ();
      GenerateRate();
      GenerateCwnd();
      GenerateTotalCwnd();
      GenerateAlpha();
#ifdef PLOT_DCTCP
      GenerateSendvsACK ();
      GenerateDctcpAlpha ();
      GenerateDctcpAlphaRtt ();
#endif
    //GeneratePktCount ();
      GeneratePlotsOutput ();
    }
  if (m_ratePlotSf)
      GenerateRateTracerSf(); //GenerateRateFile();
  if (m_ratePlotCl)
      GenerateRateTracerCl();
}

void
MpTcpSocketBase::IsLastAck ()
{
  assert(client);
  if ((subflows[0]->state >= FIN_WAIT_1 || subflows[0]->state == CLOSED) && flowCompletionTime)
    {
      int dataLeft = 0;
      int pktCount = 0;
      for (uint32_t i = 0; i < subflows.size (); i++)
        {
          dataLeft += subflows[i]->mapDSN.size ();
          pktCount += subflows[i]->PktCount;
        }

      if (pktCount == 0)
        {
          cerr << "[" << m_node->GetId () << ":" << flowId << " -> pktCount is zero!" << endl;
        }

      if (dataLeft == 0)
        {
          flowCompletionTime = false;
          DoGenerateOutPutFile ();
          GeneratePlots ();
          NotifyFlowCompleted();
        }
    }
}

uint32_t
MpTcpSocketBase::GetEstSubflows ()
{
  uint32_t c = 0;
  for (uint32_t i = 0; i < subflows.size (); i++)
    {
      if (subflows[i]->PktCount > 0)
        c++;
    }
  return c;
}


uint64_t
MpTcpSocketBase::GetPathBandwidth (uint8_t idxPath)
{
  StringValue str;
  Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();
// interface 0 is the loopback interface
  Ptr<Ipv4Interface> interface = ipv4->GetInterface (idxPath + 1);
  Ipv4InterfaceAddress interfaceAddr = interface->GetAddress (0);
// do not consider loopback addresses
  if (interfaceAddr.GetLocal () == Ipv4Address::GetLoopback ())
    return 0.0;
  Ptr<NetDevice> netDev = interface->GetDevice ();

  if (netDev->IsPointToPoint () == true)
    {
      netDev->GetAttribute (string ("DataRate"), str);
    }
  else
    {
      Ptr<Channel> link = netDev->GetChannel ();
      link->GetAttribute (string ("DataRate"), str);
    }

  DataRate bandwidth (str.Get ());
  return bandwidth.GetBitRate ();
}

void
MpTcpSocketBase::SlowDownEcnLike (uint8_t sFlowIdx)
{
  assert(m_initGamma < m_backoffBeta);
  assert(m_backoffBeta != 0 && m_initGamma != 0);
  assert(m_isDCTCPEnabled);
  SlowDownHits++;
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  double tmp = sFlow->cwnd.Get () * (1 - m_initGamma / (float) m_backoffBeta);
  if (tmp < 0) tmp = 0;
  sFlow->cwnd = std::max ((uint32_t) tmp, (m_cwndMin * sFlow->MSS));
  sFlow->ssthresh = std::max (sFlow->MSS, sFlow->cwnd.Get ());
  sFlow->dctcp_maxseq = sFlow->TxSeqNumber;

#ifdef PLOT_DCTCP
  uint32_t pkt_tmp = ((sFlow->g_AckSeqNumber - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->ECN_CWND_CUT_POINT.push_back (make_pair (Simulator::Now ().GetSeconds (), pkt_tmp));
#endif
}

void
MpTcpSocketBase::SlowDown (uint8_t sFlowIdx)
{
  assert(m_isDCTCPEnabled);
  SlowDownHits++;

  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];

  double alpha = sFlow->dctcp_alpha;

  if (m_dctcpAlphaPerAck)
    {
      alpha = sFlow->rtt->GetAlpha ();
    }

  double tmp = sFlow->cwnd.Get () * (1 - alpha / 2);
  if (tmp < 0) tmp = 0;
  sFlow->cwnd = std::max ((uint32_t) tmp, (m_cwndMin * sFlow->MSS));
  sFlow->ssthresh = std::max (sFlow->MSS, sFlow->cwnd.Get ());
  sFlow->dctcp_maxseq = sFlow->TxSeqNumber;


#ifdef PLOT_DCTCP
  uint32_t pkt_tmp = ((sFlow->g_AckSeqNumber - sFlow->initialSequnceNumber) / sFlow->MSS) % mod;
  sFlow->ECN_CWND_CUT_POINT.push_back (make_pair (Simulator::Now ().GetSeconds (), pkt_tmp));
#endif
}

void
MpTcpSocketBase::SlowDownFastReTx(uint8_t sFlowIdx, DSNMapping* ptrDSN, string sockName)
{
  NS_LOG_FUNCTION_NOARGS();
  SlowDownHits++;
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  double tmpCwnd = sFlow->cwnd.Get() * (1 - sFlow->dctcp_alpha / 2);
  sFlow->ssthresh = std::max(2 * sFlow->MSS, (uint32_t) tmpCwnd);
  sFlow->cwnd = sFlow->ssthresh + 3 * sFlow->MSS;
  DoRetransmit(sFlowIdx, ptrDSN);
//sFlow->m_recover = SequenceNumber32(sFlow->maxSeqNb + 1);
  sFlow->m_recover = SequenceNumber32(sFlow->m_highTxMark + 1);
  sFlow->m_inFastRec = true;
}

void
MpTcpSocketBase::SetDctcp (bool dctcp)
{
  m_isDCTCPEnabled = dctcp;
}

bool
MpTcpSocketBase::GetDctcp ()
{
  return m_isDCTCPEnabled;
}

void
MpTcpSocketBase::SetSlowDownEcnLike (bool sdel)
{
  m_slowDownEcnLike = sdel;
}

void
MpTcpSocketBase::SetDctcpFastAlpha (bool dctcpFastAlpha)
{
  m_dctcpFastAlpha = dctcpFastAlpha;
}



void
MpTcpSocketBase::RateTracerCl ()
{
  //double sampledGoodput = (((nextTxSequence - lastNextTxSequence) * 8) / m_rateInterval);
  double sampledGoodput = (m_totalSentBytes * 8)/m_rateInterval;
  sampledGoodput = sampledGoodput/1000000;
  rateTracerCl.push_back(make_pair(Simulator::Now().GetSeconds(), sampledGoodput));
  //lastNextTxSequence = nextTxSequence;
  m_totalSentBytes = 0;
  if (flowCompletionTime)
    m_nextRateEvent = Simulator::Schedule (Seconds (m_rateInterval), &MpTcpSocketBase::RateTracerCl, this);
}

void
MpTcpSocketBase::RecordDctcpFastRetx(uint8_t sFlowIdx, uint32_t oldCwnd)
{
  Ptr<MpTcpSubFlow> sFlow = subflows[sFlowIdx];
  uint32_t newCwnd = sFlow->cwnd.Get();
  double currentF;
  double lastF = sFlow->dctcp_last_fraction;
  double lastAlpha = sFlow->dctcp_alpha;

  if (sFlow->dctcp_total > 0)
    currentF = (double)sFlow->dctcp_marked / sFlow->dctcp_total;
  else
    currentF = 0.0;

  Ptr<OutputStreamWrapper> stream = Create<OutputStreamWrapper>(outputFileNameDctcp, std::ios::out | std::ios::app);
  ostream* os = stream->GetStream();
  *os << flowId << "\t" << m_node->GetId() << "\t" << (int) sFlow->routeId << "\t" << Simulator::Now().GetSeconds() << "\t"
      << currentF << "\t" << lastF << "\t" << lastAlpha << "\t" << oldCwnd << "\t" << newCwnd << endl;
}

void
MpTcpSocketBase::SetCapacity (string capacity)
{
  string tmp = m_capacity;
  m_capacity = capacity;

  DataRate rate = DataRate (capacity);
  uint32_t cap = rate.GetBitRate ();
  uint32_t bdp = (cap * subflows[0]->rtt->GetCurrentEstimate ().GetSeconds ()) / 8; // BDP bytes
  if (tmp > m_capacity)
    subflows[0]->ssthresh = subflows[0]->ssthresh * 2;

  cout << "current cap: " << tmp << "\tnew cap: " << cap << "\tdelay: "
      << subflows[0]->rtt->GetCurrentEstimate ().GetSeconds () << "\tBDP: "
      << bdp << endl;
}

}//namespace ns3





