/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 *   Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 *   Copyright (c) 2015, NYU WIRELESS, Tandon School of Engineering, New York University
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation;
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *   Author: Marco Miozzo <marco.miozzo@cttc.es>
 *           Nicola Baldo  <nbaldo@cttc.es>
 *   Modified by: Danilo Abrignani <danilo.abrignani@unibo.it> (Carrier Aggregation - GSoC 2015)
 *                Biljana Bojovic <biljana.bojovic@cttc.es> (Carrier Aggregation, mmwave)
 *   Modified by: Marco Mezzavilla < mezzavilla@nyu.edu> (mmwave)
 *                Sourjya Dutta <sdutta@nyu.edu> (mmwave)
 *                Russell Ford <russell.ford@nyu.edu> (mmwave)
 *                Menglei Zhang <menglei@nyu.edu> (mmwave)
 */



#include <ns3/string.h>
#include <ns3/log.h>
#include <ns3/abort.h>
#include <ns3/pointer.h>
#include <iostream>
#include <string>
#include <sstream>
#include "mmwave-helper.h"
#include <ns3/abort.h>
#include <ns3/buildings-propagation-loss-model.h>
#include <ns3/multi-model-spectrum-channel.h>
#include <ns3/uinteger.h>
#include <ns3/double.h>
#include <ns3/ipv4.h>
#include <ns3/mmwave-rrc-protocol-ideal.h>
#include <ns3/lte-rrc-protocol-real.h>
#include <ns3/epc-enb-application.h>
#include <ns3/epc-x2.h>
#include <ns3/buildings-obstacle-propagation-loss-model.h>
#include <ns3/lte-enb-component-carrier-manager.h>
#include <ns3/lte-ue-component-carrier-manager.h>
#include <ns3/bwp-manager-gnb.h>
#include <ns3/bwp-manager-ue.h>
#include <ns3/nr-ch-access-manager.h>

namespace ns3 {

/* ... */
NS_LOG_COMPONENT_DEFINE ("MmWaveHelper");

NS_OBJECT_ENSURE_REGISTERED (MmWaveHelper);

MmWaveHelper::MmWaveHelper (void)
  : m_imsiCounter (0),
  m_cellIdCounter {1},
  m_harqEnabled (false),
  m_snrTest (false)
{
  NS_LOG_FUNCTION (this);
  m_channelFactory.SetTypeId (MultiModelSpectrumChannel::GetTypeId ());
  m_enbNetDeviceFactory.SetTypeId (MmWaveEnbNetDevice::GetTypeId ());
  m_ueNetDeviceFactory.SetTypeId (MmWaveUeNetDevice::GetTypeId ());

  Config::SetDefault ("ns3::EpsBearer::Release", UintegerValue (15));
}

MmWaveHelper::~MmWaveHelper (void)
{
  NS_LOG_FUNCTION (this);
}

TypeId
MmWaveHelper::GetTypeId (void)
{
  static TypeId
    tid =
    TypeId ("ns3::MmWaveHelper")
    .SetParent<Object> ()
    .AddConstructor<MmWaveHelper> ()
    .AddAttribute ("PathlossModel",
                   "The type of path-loss model to be used. "
                   "The allowed values for this attributes are the type names "
                   "of any class inheriting from ns3::PropagationLossModel.",
                   StringValue ("ns3::MmWavePropagationLossModel"),
                   MakeStringAccessor (&MmWaveHelper::SetPathlossModelType),
                   MakeStringChecker ())
    .AddAttribute ("ChannelModel",
                   "The type of MIMO channel model to be used. "
                   "The allowed values for this attributes are the type names "
                   "of any class inheriting from ns3::SpectrumPropagationLossModel.",
                   StringValue ("ns3::MmWaveBeamforming"),
                   MakeStringAccessor (&MmWaveHelper::SetChannelModelType),
                   MakeStringChecker ())
    .AddAttribute ("HarqEnabled",
                   "Enable Hybrid ARQ",
                   BooleanValue (true),
                   MakeBooleanAccessor (&MmWaveHelper::m_harqEnabled),
                   MakeBooleanChecker ())
    .AddAttribute ("UseCa",
                   "If true, Carrier Aggregation feature is enabled and a valid Component Carrier Map is expected."
                   "If false, single carrier simulation.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MmWaveHelper::m_useCa),
                   MakeBooleanChecker ())
    .AddAttribute ("NumberOfComponentCarriers",
                   "Set the number of Component carrier to use "
                   "If it is more than one and m_useCa is false, it will raise an error ",
                   UintegerValue (1),
                   MakeUintegerAccessor (&MmWaveHelper::m_noOfCcs),
                   MakeUintegerChecker<uint16_t> (MIN_NO_CC, MAX_NO_CC))
    ;
  return tid;
}

void
MmWaveHelper::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_phyStats = nullptr;
  m_bwpConfiguration.clear ();
  Object::DoDispose ();
}

void
MmWaveHelper::DoInitialize ()
{
  NS_LOG_FUNCTION (this);
  NS_ABORT_MSG_IF (m_pathlossModelType.empty (), "You forget to set a Pathloss model");
  NS_ABORT_MSG_IF (m_channelModelType != "ns3::MmWave3gppChannel", "Cannot set a different type of channel");

  if (m_bwpConfiguration.empty())
    {
      Ptr<MmWavePhyMacCommon> phyMacCommon = CreateObject <MmWavePhyMacCommon> ();
      m_bwpConfiguration.emplace (std::make_pair (0, BandwidthPartRepresentation (0, phyMacCommon, nullptr, nullptr, nullptr)));
    }

  NS_ASSERT (! m_bwpConfiguration.empty ());
  for (auto & conf : m_bwpConfiguration)
    {
      if (conf.second.m_channel == nullptr && conf.second.m_propagation == nullptr && conf.second.m_3gppChannel == nullptr)
        {
          // Create everything inside, and connect things
          NS_ABORT_UNLESS (m_pathlossModelType == "ns3::MmWave3gppBuildingsPropagationLossModel" || m_pathlossModelType == "ns3::MmWave3gppPropagationLossModel");
          conf.second.m_channel = m_channelFactory.Create<SpectrumChannel> ();
          conf.second.m_propagation = DynamicCast<PropagationLossModel> (m_pathlossModelFactory.Create ());
          conf.second.m_propagation->SetAttributeFailSafe("Frequency", DoubleValue(conf.second.m_phyMacCommon->GetCenterFrequency()));
          conf.second.m_channel->AddPropagationLossModel (conf.second.m_propagation);

          conf.second.m_3gppChannel = CreateObject<MmWave3gppChannel> ();
          conf.second.m_3gppChannel->SetPathlossModel (conf.second.m_propagation);
          conf.second.m_3gppChannel->SetAttribute ("CenterFrequency", DoubleValue (conf.second.m_phyMacCommon->GetCenterFrequency()));

          conf.second.m_channel->AddSpectrumPropagationLossModel (conf.second.m_3gppChannel);
        }
      else if (conf.second.m_channel != nullptr && conf.second.m_propagation != nullptr && conf.second.m_3gppChannel != nullptr)
        {
          // We suppose that the channel and the propagation are correctly connected
          // outside
          NS_LOG_INFO ("Channel and propagation received as input");
        }
      else
        {
          NS_FATAL_ERROR ("Configuration not supported");
        }

      NS_ASSERT (conf.second.m_channel != nullptr);
      NS_ASSERT (conf.second.m_propagation != nullptr);
      NS_ASSERT (conf.second.m_3gppChannel != nullptr);
    }

  m_phyStats = CreateObject<MmWavePhyRxTrace> ();

  m_initialized = true;

  Object::DoInitialize ();
}

void
MmWaveHelper::SetPathlossModelType (std::string type)
{
  NS_LOG_FUNCTION (this << type);
  m_pathlossModelType = type;
  if (!type.empty ())
    {
      m_pathlossModelFactory = ObjectFactory ();
      m_pathlossModelFactory.SetTypeId (type);
    }
}

Ptr<PropagationLossModel>
MmWaveHelper::GetPathLossModel (uint8_t index)
{
  return m_pathlossModel.at (index)->GetObject<PropagationLossModel> ();
}

void
MmWaveHelper::AddBandwidthPart (uint32_t id, const BandwidthPartRepresentation &bwpRepr)
{
  NS_LOG_FUNCTION (this);
  auto it = m_bwpConfiguration.find (id);
  if (it != m_bwpConfiguration.end ())
    {
      NS_FATAL_ERROR ("Bad BWP configuration: You already configured bwp id " << id);
    }

  NS_ASSERT (id == bwpRepr.m_id);
  m_bwpConfiguration.emplace (std::make_pair (id, bwpRepr));
}

void
MmWaveHelper::SetChannelModelType (std::string type)
{
  NS_LOG_FUNCTION (this << type);
  m_channelModelType = type;
}

uint32_t
MmWaveHelper::GetNumberBwp (const Ptr<const NetDevice> &gnbDevice)
{
  NS_LOG_FUNCTION (gnbDevice);
  Ptr<const MmWaveEnbNetDevice> netDevice = DynamicCast<const MmWaveEnbNetDevice> (gnbDevice);
  if (netDevice == nullptr)
    {
      return 0;
    }
  return netDevice->GetCcMapSize ();
}

Ptr<MmWaveEnbPhy>
MmWaveHelper::GetEnbPhy (const Ptr<NetDevice> &gnbDevice, uint32_t bwpIndex)
{
  NS_LOG_FUNCTION (gnbDevice << bwpIndex);
  NS_ASSERT(bwpIndex < UINT8_MAX);
  Ptr<MmWaveEnbNetDevice> netDevice = DynamicCast<MmWaveEnbNetDevice> (gnbDevice);
  if (netDevice == nullptr)
    {
      return nullptr;
    }
  return netDevice->GetPhy (static_cast<uint8_t> (bwpIndex));
}

Ptr<MmWaveEnbMac>
MmWaveHelper::GetEnbMac (const Ptr<NetDevice> &gnbDevice, uint32_t bwpIndex)
{
  NS_LOG_FUNCTION (gnbDevice << bwpIndex);
  NS_ASSERT(bwpIndex < UINT8_MAX);
  Ptr<MmWaveEnbNetDevice> netDevice = DynamicCast<MmWaveEnbNetDevice> (gnbDevice);
  if (netDevice == nullptr)
    {
      return nullptr;
    }
  return netDevice->GetMac (static_cast<uint8_t> (bwpIndex));
}

void
MmWaveHelper::SetSchedulerType (std::string type)
{
  NS_LOG_FUNCTION (this << type);
  m_defaultSchedulerType = TypeId::LookupByName (type);
}

void
MmWaveHelper::SetHarqEnabled (bool harqEnabled)
{
  m_harqEnabled = harqEnabled;
}

bool
MmWaveHelper::GetHarqEnabled ()
{
  return m_harqEnabled;
}

void
MmWaveHelper::SetSnrTest (bool snrTest)
{
  m_snrTest = snrTest;
}

bool
MmWaveHelper::GetSnrTest ()
{
  return m_snrTest;
}

NetDeviceContainer
MmWaveHelper::InstallUeDevice (NodeContainer c)
{
  NS_LOG_FUNCTION (this);
  Initialize ();    // Run DoInitialize (), if necessary
  NetDeviceContainer devices;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;
      Ptr<NetDevice> device = InstallSingleUeDevice (node);
      device->SetAddress (Mac48Address::Allocate ());
      devices.Add (device);
    }
  return devices;

}

NetDeviceContainer
MmWaveHelper::InstallEnbDevice (NodeContainer c)
{
  NS_LOG_FUNCTION (this);
  Initialize ();    // Run DoInitialize (), if necessary
  NetDeviceContainer devices;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;
      Ptr<NetDevice> device = InstallSingleEnbDevice (node);
      device->SetAddress (Mac48Address::Allocate ());
      devices.Add (device);
    }
  return devices;
}

Ptr<NetDevice>
MmWaveHelper::InstallSingleUeDevice (Ptr<Node> n)
{
  NS_LOG_FUNCTION (this);

  Ptr<MmWaveUeNetDevice> dev = m_ueNetDeviceFactory.Create<MmWaveUeNetDevice> ();
  std::map<uint8_t, Ptr<ComponentCarrierMmWaveUe> > ueCcMap;

  // Create, for each ue, its component carriers
  for (const auto &conf : m_bwpConfiguration)
    {
      Ptr <ComponentCarrierMmWaveUe> cc =  CreateObject<ComponentCarrierMmWaveUe> ();
      cc->SetUlBandwidth (conf.second.m_phyMacCommon->GetBandwidth ());
      cc->SetDlBandwidth (conf.second.m_phyMacCommon->GetBandwidth ());
      cc->SetDlEarfcn (conf.first + 1);
      cc->SetUlEarfcn (conf.first + 1);

      if (conf.second.m_id == 0)
        {
          cc->SetAsPrimary (true);
        }
      else
        {
          cc->SetAsPrimary (false);
        }
      Ptr<MmWaveUeMac> mac = CreateObject<MmWaveUeMac> ();
      cc->SetMac (mac);
      // cc->GetPhy ()->Initialize (); // it is initialized within the LteUeNetDevice::DoInitialize ()
      ueCcMap.insert (std::make_pair (conf.first, cc));
    }

  ObjectFactory channelAccessManagerFactory;

  for (auto it = ueCcMap.begin (); it != ueCcMap.end (); ++it)
    {
      BandwidthPartRepresentation & conf = m_bwpConfiguration.at (it->first);
      NS_ASSERT (conf.m_id == it->first);

      Ptr<MmWaveSpectrumPhy> channelPhy = CreateObject<MmWaveSpectrumPhy> ();
      Ptr<MmWaveUePhy> phy = CreateObject<MmWaveUePhy> (channelPhy, n);
      Ptr<MmWaveHarqPhy> harq = Create<MmWaveHarqPhy> (conf.m_phyMacCommon->GetNumHarqProcess ());

      channelAccessManagerFactory.SetTypeId (conf.m_ueChannelAccessManagerType);
      Ptr<NrChAccessManager> cam = DynamicCast<NrChAccessManager> (channelAccessManagerFactory.Create ());
      cam->SetNrSpectrumPhy (channelPhy);
      phy->SetCam (cam);

      channelPhy->SetHarqPhyModule (harq);

      Ptr<mmWaveChunkProcessor> pData = Create<mmWaveChunkProcessor> ();
      pData->AddCallback (MakeCallback (&MmWaveUePhy::GenerateDlCqiReport, phy));
      pData->AddCallback (MakeCallback (&MmWaveSpectrumPhy::UpdateSinrPerceived, channelPhy));
      channelPhy->AddDataSinrChunkProcessor (pData);

      if (m_harqEnabled)
        {
          channelPhy->SetPhyDlHarqFeedbackCallback (MakeCallback (&MmWaveUePhy::ReceiveLteDlHarqFeedback, phy));
        }

      channelPhy->SetChannel (conf.m_channel);

      Ptr<MobilityModel> mm = n->GetObject<MobilityModel> ();
      NS_ASSERT_MSG (mm, "MobilityModel needs to be set on node before calling MmWaveHelper::InstallUeDevice ()");
      channelPhy->SetMobility (mm);

      channelPhy->SetPhyRxDataEndOkCallback (MakeCallback (&MmWaveUePhy::PhyDataPacketReceived, phy));
      channelPhy->SetPhyRxCtrlEndOkCallback (MakeCallback (&MmWaveUePhy::PhyCtrlMessagesReceived, phy));

      it->second->SetPhy (phy);
    }

  Ptr<LteUeComponentCarrierManager> ccmUe = DynamicCast<LteUeComponentCarrierManager> (CreateObject <BwpManagerUe> ());

  NS_ABORT_IF(m_noOfCcs != m_bwpConfiguration.size ());

  Ptr<LteUeRrc> rrc = CreateObject<LteUeRrc> ();
  rrc->m_numberOfComponentCarriers = m_noOfCcs;
  // run intializeSap to create the proper number of sap provider/users
  rrc->InitializeSap ();
  rrc->SetLteMacSapProvider (ccmUe->GetLteMacSapProvider ());
  // setting ComponentCarrierManager SAP
  rrc->SetLteCcmRrcSapProvider (ccmUe->GetLteCcmRrcSapProvider ());
  ccmUe->SetLteCcmRrcSapUser (rrc->GetLteCcmRrcSapUser ());
  ccmUe->SetNumberOfComponentCarriers (m_noOfCcs);

  bool useIdealRrc = true;
  if (useIdealRrc)
    {
      Ptr<mmWaveUeRrcProtocolIdeal> rrcProtocol = CreateObject<mmWaveUeRrcProtocolIdeal> ();
      rrcProtocol->SetUeRrc (rrc);
      rrc->AggregateObject (rrcProtocol);
      rrcProtocol->SetLteUeRrcSapProvider (rrc->GetLteUeRrcSapProvider ());
      rrc->SetLteUeRrcSapUser (rrcProtocol->GetLteUeRrcSapUser ());
    }
  else
    {
      Ptr<LteUeRrcProtocolReal> rrcProtocol = CreateObject<LteUeRrcProtocolReal> ();
      rrcProtocol->SetUeRrc (rrc);
      rrc->AggregateObject (rrcProtocol);
      rrcProtocol->SetLteUeRrcSapProvider (rrc->GetLteUeRrcSapProvider ());
      rrc->SetLteUeRrcSapUser (rrcProtocol->GetLteUeRrcSapUser ());
    }

  if (m_epcHelper != 0)
    {
      rrc->SetUseRlcSm (false);
    }
  else
    {
      rrc->SetUseRlcSm (true);
    }
  Ptr<EpcUeNas> nas = CreateObject<EpcUeNas> ();

  nas->SetAsSapProvider (rrc->GetAsSapProvider ());
  rrc->SetAsSapUser (nas->GetAsSapUser ());

  for (auto it = ueCcMap.begin (); it != ueCcMap.end (); ++it)
    {
      NS_ASSERT (it->first == m_bwpConfiguration.at (it->first).m_id);
      Ptr<MmWavePhyMacCommon> phyMacCommon = m_bwpConfiguration.at (it->first).m_phyMacCommon;
      rrc->SetLteUeCmacSapProvider (it->second->GetMac ()->GetUeCmacSapProvider (), it->first);
      it->second->GetMac ()->SetUeCmacSapUser (rrc->GetLteUeCmacSapUser (it->first));

      it->second->GetPhy ()->SetUeCphySapUser (rrc->GetLteUeCphySapUser ());
      rrc->SetLteUeCphySapProvider (it->second->GetPhy ()->GetUeCphySapProvider (), it->first);

      it->second->GetMac ()->SetConfigurationParameters (phyMacCommon);

      it->second->GetPhy ()->SetPhySapUser (it->second->GetMac ()->GetPhySapUser ());
      it->second->GetMac ()->SetPhySapProvider (it->second->GetPhy ()->GetPhySapProvider ());

      bool ccmTest = ccmUe->SetComponentCarrierMacSapProviders (it->first,
                                                                it->second->GetMac ()->GetUeMacSapProvider ());

      if (ccmTest == false)
        {
          NS_FATAL_ERROR ("Error in SetComponentCarrierMacSapProviders");
        }
    }

  NS_ABORT_MSG_IF (m_imsiCounter >= 0xFFFFFFFF, "max num UEs exceeded");
  uint64_t imsi = ++m_imsiCounter;

  dev->SetNode (n);
  dev->SetAttribute ("Imsi", UintegerValue (imsi));
  dev->SetCcMap (ueCcMap);
  //dev->SetAttribute ("MmWaveUePhy", PointerValue(phy));
  //dev->SetAttribute ("MmWaveUeMac", PointerValue(mac));
  dev->SetAttribute ("mmWaveUeRrc", PointerValue (rrc));
  dev->SetAttribute ("EpcUeNas", PointerValue (nas));
  dev->SetAttribute ("LteUeComponentCarrierManager", PointerValue (ccmUe));

  for (std::map<uint8_t, Ptr<ComponentCarrierMmWaveUe> >::iterator it = ueCcMap.begin (); it != ueCcMap.end (); ++it)
    {
      Ptr<MmWaveUePhy> ccPhy = it->second->GetPhy ();
      ccPhy->SetDevice (dev);
      ccPhy->GetSpectrumPhy ()->SetDevice (dev);
      // hooks are earlier set
    }

  nas->SetDevice (dev);

  n->AddDevice (dev);

  nas->SetForwardUpCallback (MakeCallback (&MmWaveUeNetDevice::Receive, dev));

  if (m_epcHelper != 0)
    {
      m_epcHelper->AddUe (dev, dev->GetImsi ());
    }

  dev->Initialize ();

  return dev;
}

Ptr<NetDevice>
MmWaveHelper::InstallSingleEnbDevice (Ptr<Node> n)
{
  NS_ABORT_MSG_IF (m_cellIdCounter == 65535, "max num eNBs exceeded");
  NS_ASSERT (m_initialized);

  uint16_t cellId = m_cellIdCounter; // \todo Remove, eNB has no cell ID, now each carrier has cell ID

  Ptr<MmWaveEnbNetDevice> dev = m_enbNetDeviceFactory.Create<MmWaveEnbNetDevice> ();

  // create component carrier map for this eNb device
  std::map<uint8_t,Ptr<ComponentCarrierGnb> > ccMap;

  for (const auto & conf : m_bwpConfiguration)
    {
      NS_ASSERT (conf.second.m_channel != nullptr);
      Ptr <ComponentCarrierGnb> cc =  CreateObject<ComponentCarrierGnb> ();
      cc->SetUlBandwidth (conf.second.m_phyMacCommon->GetBandwidth ());
      cc->SetDlBandwidth (conf.second.m_phyMacCommon->GetBandwidth ());
      cc->SetDlEarfcn (conf.first + 1);
      cc->SetUlEarfcn (conf.first + 1);

      if (conf.second.m_id == 0)
        {
          cc->SetAsPrimary (true);
        }
      else
        {
          cc->SetAsPrimary (false);
        }

      NS_ABORT_MSG_IF (m_cellIdCounter == 65535, "max num cells exceeded");
      cc->SetCellId (m_cellIdCounter++);
      ccMap.insert (std::make_pair (conf.first, cc));
    }

  ObjectFactory channelAccessManagerFactory;

  for (auto it = ccMap.begin (); it != ccMap.end (); ++it)
    {
      BandwidthPartRepresentation &conf = m_bwpConfiguration.at(it->first);
      NS_ASSERT (conf.m_id == it->first);
      Ptr<MmWaveSpectrumPhy> channelPhy = CreateObject<MmWaveSpectrumPhy> ();
      Ptr<MmWaveEnbPhy> phy = CreateObject<MmWaveEnbPhy> (channelPhy, n);

      MmWaveEnbPhy::PerformBeamformingFn beamformingFn;
      beamformingFn = std::bind (&MmWave3gppChannel::PerformBeamforming, conf.m_3gppChannel,
                                 std::placeholders::_1, std::placeholders::_2);
      phy->SetPerformBeamformingFn (beamformingFn);

      // PHY <--> CAM
      channelAccessManagerFactory.SetTypeId (conf.m_gnbChannelAccessManagerType);
      Ptr<NrChAccessManager> cam = DynamicCast<NrChAccessManager> (channelAccessManagerFactory.Create ());
      cam->SetNrSpectrumPhy (channelPhy);
      phy->SetCam (cam);

      Ptr<MmWaveHarqPhy> harq = Create<MmWaveHarqPhy> (conf.m_phyMacCommon->GetNumHarqProcess ());
      channelPhy->SetHarqPhyModule (harq);

      Ptr<mmWaveChunkProcessor> pData = Create<mmWaveChunkProcessor> ();
      if (!m_snrTest)
        {
          pData->AddCallback (MakeCallback (&MmWaveEnbPhy::GenerateDataCqiReport, phy));
          pData->AddCallback (MakeCallback (&MmWaveSpectrumPhy::UpdateSinrPerceived, channelPhy));
        }
      channelPhy->AddDataSinrChunkProcessor (pData);

      phy->SetConfigurationParameters (conf.m_phyMacCommon);
      phy->SetTddPattern (conf.m_pattern);

      channelPhy->SetChannel (conf.m_channel);

      Ptr<MobilityModel> mm = n->GetObject<MobilityModel> ();
      NS_ASSERT_MSG (mm, "MobilityModel needs to be set on node before calling MmWaveHelper::InstallEnbDevice ()");
      channelPhy->SetMobility (mm);

      Ptr<MmWaveEnbMac> mac = CreateObject<MmWaveEnbMac> ();
      mac->SetConfigurationParameters (conf.m_phyMacCommon);

      ObjectFactory schedFactory;
      schedFactory.SetTypeId (m_defaultSchedulerType);
      schedFactory.SetTypeId (conf.m_phyMacCommon->GetMacSchedType ());
      Ptr<MmWaveMacScheduler> sched = DynamicCast<MmWaveMacScheduler> (schedFactory.Create ());

      sched->ConfigureCommonParameters (conf.m_phyMacCommon);
      it->second->SetMac (mac);
      it->second->SetMmWaveMacScheduler (sched);
      it->second->SetPhy (phy);

      cam->SetNrEnbMac (mac);
    }

  NS_ABORT_MSG_IF (m_useCa && ccMap.size () < 2, "You have to either specify carriers or disable carrier aggregation");

  NS_ASSERT (ccMap.size () == m_noOfCcs);

  Ptr<LteEnbRrc> rrc = CreateObject<LteEnbRrc> ();
  Ptr<LteEnbComponentCarrierManager> ccmEnbManager = DynamicCast<LteEnbComponentCarrierManager> (CreateObject<BwpManagerGnb> ());

  // Convert Enb carrier map to only PhyConf map
  // we want to make RRC to be generic, to be able to work with any type of carriers, not only strictly LTE carriers
  std::map < uint8_t, Ptr<ComponentCarrierBaseStation> > ccPhyConfMap;
  for (const auto &i : ccMap)
    {
      Ptr<ComponentCarrierBaseStation> c = i.second;
      ccPhyConfMap.insert (std::pair<uint8_t, Ptr<ComponentCarrierBaseStation> > (i.first,c));
    }

  //ComponentCarrierManager SAP
  rrc->SetLteCcmRrcSapProvider (ccmEnbManager->GetLteCcmRrcSapProvider ());
  ccmEnbManager->SetLteCcmRrcSapUser (rrc->GetLteCcmRrcSapUser ());
  // Set number of component carriers. Note: eNB CCM would also set the
  // number of component carriers in eNB RRC

  ccmEnbManager->SetNumberOfComponentCarriers (m_noOfCcs);
  rrc->ConfigureCarriers (ccPhyConfMap);

  //mmwave module currently uses only RRC ideal mode
  bool useIdealRrc = true;

  if (useIdealRrc)
    {
      Ptr<MmWaveEnbRrcProtocolIdeal> rrcProtocol = CreateObject<MmWaveEnbRrcProtocolIdeal> ();
      rrcProtocol->SetLteEnbRrcSapProvider (rrc->GetLteEnbRrcSapProvider ());
      rrc->SetLteEnbRrcSapUser (rrcProtocol->GetLteEnbRrcSapUser ());
      rrc->AggregateObject (rrcProtocol);
      rrcProtocol->SetCellId (cellId);
    }
  else
    {
      Ptr<LteEnbRrcProtocolReal> rrcProtocol = CreateObject<LteEnbRrcProtocolReal> ();
      rrcProtocol->SetLteEnbRrcSapProvider (rrc->GetLteEnbRrcSapProvider ());
      rrc->SetLteEnbRrcSapUser (rrcProtocol->GetLteEnbRrcSapUser ());
      rrc->AggregateObject (rrcProtocol);
      rrcProtocol->SetCellId (cellId);
    }

  if (m_epcHelper != 0)
    {
      EnumValue epsBearerToRlcMapping;
      rrc->GetAttribute ("EpsBearerToRlcMapping", epsBearerToRlcMapping);
      // it does not make sense to use RLC/SM when also using the EPC
      if (epsBearerToRlcMapping.Get () == LteEnbRrc::RLC_SM_ALWAYS)
        {
          rrc->SetAttribute ("EpsBearerToRlcMapping", EnumValue (LteEnbRrc::RLC_UM_ALWAYS));
        }
    }

  // This RRC attribute is used to connect each new RLC instance with the MAC layer
  // (for function such as TransmitPdu, ReportBufferStatusReport).
  // Since in this new architecture, the component carrier manager acts a proxy, it
  // will have its own LteMacSapProvider interface, RLC will see it as through original MAC
  // interface LteMacSapProvider, but the function call will go now through LteEnbComponentCarrierManager
  // instance that needs to implement functions of this interface, and its task will be to
  // forward these calls to the specific MAC of some of the instances of component carriers. This
  // decision will depend on the specific implementation of the component carrier manager.
  rrc->SetLteMacSapProvider (ccmEnbManager->GetLteMacSapProvider ());

  bool ccmTest;
  for (std::map<uint8_t,Ptr<ComponentCarrierGnb> >::iterator it = ccMap.begin (); it != ccMap.end (); ++it)
    {
      it->second->GetPhy ()->SetEnbCphySapUser (rrc->GetLteEnbCphySapUser (it->first));
      rrc->SetLteEnbCphySapProvider (it->second->GetPhy ()->GetEnbCphySapProvider (), it->first);

      rrc->SetLteEnbCmacSapProvider (it->second->GetMac ()->GetEnbCmacSapProvider (),it->first );
      it->second->GetMac ()->SetEnbCmacSapUser (rrc->GetLteEnbCmacSapUser (it->first));

      //FFR SAP - currently not used in mmwave module
      //it->second->GetFfMacScheduler ()->SetLteFfrSapProvider (it->second->GetFfrAlgorithm ()->GetLteFfrSapProvider ());
      //      it->second->GetFfrAlgorithm ()->SetLteFfrSapUser (it->second->GetFfMacScheduler ()->GetLteFfrSapUser ());
      //rrc->SetLteFfrRrcSapProvider (it->second->GetFfrAlgorithm ()->GetLteFfrRrcSapProvider (), it->first);
      //it->second->GetFfrAlgorithm ()->SetLteFfrRrcSapUser (rrc->GetLteFfrRrcSapUser (it->first));

      // PHY <--> MAC SAP
      it->second->GetPhy ()->SetPhySapUser (it->second->GetMac ()->GetPhySapUser ());
      it->second->GetMac ()->SetPhySapProvider (it->second->GetPhy ()->GetPhySapProvider ());
      // PHY <--> MAC SAP END

      //Scheduler SAP
      it->second->GetMac ()->SetMmWaveMacSchedSapProvider (it->second->GetMmWaveMacScheduler ()->GetMacSchedSapProvider ());
      it->second->GetMac ()->SetMmWaveMacCschedSapProvider (it->second->GetMmWaveMacScheduler ()->GetMacCschedSapProvider ());

      it->second->GetMmWaveMacScheduler ()->SetMacSchedSapUser (it->second->GetMac ()->GetMmWaveMacSchedSapUser ());
      it->second->GetMmWaveMacScheduler ()->SetMacCschedSapUser (it->second->GetMac ()->GetMmWaveMacCschedSapUser ());
      // Scheduler SAP END

      it->second->GetMac ()->SetLteCcmMacSapUser (ccmEnbManager->GetLteCcmMacSapUser ());
      ccmEnbManager->SetCcmMacSapProviders (it->first, it->second->GetMac ()->GetLteCcmMacSapProvider ());

      // insert the pointer to the LteMacSapProvider interface of the MAC layer of the specific component carrier
      ccmTest = ccmEnbManager->SetMacSapProvider (it->first, it->second->GetMac ()->GetMacSapProvider ());

      if (ccmTest == false)
        {
          NS_FATAL_ERROR ("Error in SetComponentCarrierMacSapProviders");
        }
    }

  dev->SetNode (n);
  dev->SetAttribute ("CellId", UintegerValue (cellId));
  dev->SetAttribute ("LteEnbComponentCarrierManager", PointerValue (ccmEnbManager));
  // this is set by component carrier map
  // dev->SetAttribute ("MmWaveEnbPhy", PointerValue (phy));
  // dev->SetAttribute ("MmWaveEnbMac", PointerValue (mac));
  // dev->SetAttribute ("mmWaveScheduler", PointerValue(sched));
  dev->SetCcMap (ccMap);
  std::map<uint8_t,Ptr<ComponentCarrierGnb> >::iterator it = ccMap.begin ();
  dev->SetAttribute ("LteEnbRrc", PointerValue (rrc));
  //dev->SetAttribute ("LteHandoverAlgorithm", PointerValue (handoverAlgorithm));
  //dev->SetAttribute ("LteFfrAlgorithm", PointerValue (it->second->GetFfrAlgorithm ()));

// ANR not supported in mmwave
//  if (m_isAnrEnabled)
//    {
//      Ptr<LteAnr> anr = CreateObject<LteAnr> (cellId);
//      rrc->SetLteAnrSapProvider (anr->GetLteAnrSapProvider ());
//      anr->SetLteAnrSapUser (rrc->GetLteAnrSapUser ());
//      dev->SetAttribute ("LteAnr", PointerValue (anr));
//    }

  for (it = ccMap.begin (); it != ccMap.end (); ++it)
    {
      Ptr<MmWaveEnbPhy> ccPhy = it->second->GetPhy ();
      ccPhy->SetDevice (dev);
      ccPhy->GetSpectrumPhy ()->SetDevice (dev);
      ccPhy->GetSpectrumPhy ()->SetCellId (cellId);
      ccPhy->GetSpectrumPhy ()->SetPhyRxDataEndOkCallback (MakeCallback (&MmWaveEnbPhy::PhyDataPacketReceived, ccPhy));
      ccPhy->GetSpectrumPhy ()->SetPhyRxCtrlEndOkCallback (MakeCallback (&MmWaveEnbPhy::PhyCtrlMessagesReceived, ccPhy));
      ccPhy->GetSpectrumPhy ()->SetPhyUlHarqFeedbackCallback (MakeCallback (&MmWaveEnbPhy::ReceiveUlHarqFeedback, ccPhy));
      NS_LOG_LOGIC ("set the propagation model frequencies");
    }  //end for
  rrc->SetForwardUpCallback (MakeCallback (&MmWaveEnbNetDevice::Receive, dev));
  dev->Initialize ();
  n->AddDevice (dev);

  for (const auto & conf : m_bwpConfiguration)
    {
      conf.second.m_channel->AddRx(ccMap.at (conf.second.m_id)->GetPhy ()->GetSpectrumPhy ());
      Ptr<AntennaArrayBasicModel> antenna = dev->GetPhy (conf.first)->GetAntennaArray();
      conf.second.m_3gppChannel->RegisterDevicesAntennaArray (dev, antenna);
    }

  if (m_epcHelper != 0)
    {
      NS_LOG_INFO ("adding this eNB to the EPC");
      m_epcHelper->AddEnb (n, dev, dev->GetCellId ());
      Ptr<EpcEnbApplication> enbApp = n->GetApplication (0)->GetObject<EpcEnbApplication> ();
      NS_ASSERT_MSG (enbApp != 0, "cannot retrieve EpcEnbApplication");

      // S1 SAPs
      rrc->SetS1SapProvider (enbApp->GetS1SapProvider ());
      enbApp->SetS1SapUser (rrc->GetS1SapUser ());

      // X2 SAPs
      Ptr<EpcX2> x2 = n->GetObject<EpcX2> ();
      x2->SetEpcX2SapUser (rrc->GetEpcX2SapUser ());
      rrc->SetEpcX2SapProvider (x2->GetEpcX2SapProvider ());
    }

  return dev;
}

void
MmWaveHelper::AttachToClosestEnb (NetDeviceContainer ueDevices, NetDeviceContainer enbDevices)
{
  NS_LOG_FUNCTION (this);

  for (NetDeviceContainer::Iterator i = ueDevices.Begin (); i != ueDevices.End (); i++)
    {
      AttachToClosestEnb (*i, enbDevices);
    }
}

void
MmWaveHelper::AttachToClosestEnb (Ptr<NetDevice> ueDevice, NetDeviceContainer enbDevices)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT_MSG (enbDevices.GetN () > 0, "empty enb device container");
  Vector uepos = ueDevice->GetNode ()->GetObject<MobilityModel> ()->GetPosition ();
  double minDistance = std::numeric_limits<double>::infinity ();
  Ptr<NetDevice> closestEnbDevice;
  for (NetDeviceContainer::Iterator i = enbDevices.Begin (); i != enbDevices.End (); ++i)
    {
      Vector enbpos = (*i)->GetNode ()->GetObject<MobilityModel> ()->GetPosition ();
      double distance = CalculateDistance (uepos, enbpos);
      if (distance < minDistance)
        {
          minDistance = distance;
          closestEnbDevice = *i;
        }
    }
  NS_ASSERT (closestEnbDevice != 0);

  AttachToEnb (ueDevice, closestEnbDevice);
}


void
MmWaveHelper::AttachToEnb (const Ptr<NetDevice> &ueDevice,
                           const Ptr<NetDevice> &gnbDevice)
{
  auto enbNetDev = gnbDevice->GetObject<MmWaveEnbNetDevice> ();
  auto ueNetDev = ueDevice->GetObject<MmWaveUeNetDevice> ();

  NS_ABORT_IF (enbNetDev == nullptr || ueNetDev == nullptr);

  for (uint32_t i = 0; i < enbNetDev->GetCcMapSize (); ++i)
    {
      Ptr<MmWavePhyMacCommon> configParams = enbNetDev->GetPhy (i)->GetConfigurationParameters ();
      (DynamicCast<MmWaveEnbPhy>(enbNetDev->GetPhy(i)))->RegisterUe (ueDevice->GetObject<MmWaveUeNetDevice> ()->GetImsi (), ueDevice);
      (DynamicCast<MmWaveUePhy>(ueNetDev->GetPhy (i)))->RegisterToEnb (enbNetDev->GetCellId (i), configParams);
      Ptr<EpcUeNas> ueNas = ueDevice->GetObject<MmWaveUeNetDevice> ()->GetNas ();
      ueNas->Connect (gnbDevice->GetObject<MmWaveEnbNetDevice> ()->GetCellId (i),
                      gnbDevice->GetObject<MmWaveEnbNetDevice> ()->GetEarfcn (i));
    }

  if (m_epcHelper != 0)
    {
      // activate default EPS bearer
      m_epcHelper->ActivateEpsBearer (ueDevice, ueDevice->GetObject<MmWaveUeNetDevice> ()->GetImsi (), EpcTft::Default (), EpsBearer (EpsBearer::NGBR_VIDEO_TCP_DEFAULT));
    }

  // tricks needed for the simplified LTE-only simulations
  //if (m_epcHelper == 0)
  //{
  ueNetDev->SetTargetEnb (enbNetDev);
  //}

    for (const auto &it : m_bwpConfiguration)
      {
        NS_ABORT_IF (it.second.m_3gppChannel == nullptr);
        Ptr<AntennaArrayBasicModel> ueAntenna = ueNetDev->GetPhy(it.first)->GetAntennaArray();
        it.second.m_3gppChannel->RegisterDevicesAntennaArray (ueNetDev, ueAntenna, true);
      }
}



uint8_t
MmWaveHelper::ActivateDedicatedEpsBearer (NetDeviceContainer ueDevices, EpsBearer bearer, Ptr<EpcTft> tft)
{
  NS_LOG_FUNCTION (this);
  for (NetDeviceContainer::Iterator i = ueDevices.Begin (); i != ueDevices.End (); ++i)
    {
      uint8_t bearerId = ActivateDedicatedEpsBearer (*i, bearer, tft);
      return bearerId;
    }
  return 0;
}


uint8_t
MmWaveHelper::ActivateDedicatedEpsBearer (Ptr<NetDevice> ueDevice, EpsBearer bearer, Ptr<EpcTft> tft)
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT_MSG (m_epcHelper != 0, "dedicated EPS bearers cannot be set up when the EPC is not used");

  uint64_t imsi = ueDevice->GetObject<MmWaveUeNetDevice> ()->GetImsi ();
  uint8_t bearerId = m_epcHelper->ActivateEpsBearer (ueDevice, imsi, tft, bearer);
  return bearerId;
}

void
MmWaveHelper::DeActivateDedicatedEpsBearer (Ptr<NetDevice> ueDevice,Ptr<NetDevice> enbDevice, uint8_t bearerId)
{
  NS_LOG_FUNCTION (this << ueDevice << bearerId);
  NS_ASSERT_MSG (m_epcHelper != 0, "Dedicated EPS bearers cannot be de-activated when the EPC is not used");
  NS_ASSERT_MSG (bearerId != 1, "Default bearer cannot be de-activated until and unless and UE is released");

  DoDeActivateDedicatedEpsBearer (ueDevice, enbDevice, bearerId);
}

void
MmWaveHelper::DoDeActivateDedicatedEpsBearer (Ptr<NetDevice> ueDevice, Ptr<NetDevice> enbDevice, uint8_t bearerId)
{
  NS_LOG_FUNCTION (this << ueDevice << bearerId);

  //Extract IMSI and rnti
  uint64_t imsi = ueDevice->GetObject<MmWaveUeNetDevice> ()->GetImsi ();
  uint16_t rnti = ueDevice->GetObject<MmWaveUeNetDevice> ()->GetRrc ()->GetRnti ();


  Ptr<LteEnbRrc> enbRrc = enbDevice->GetObject<MmWaveEnbNetDevice> ()->GetRrc ();

  enbRrc->DoSendReleaseDataRadioBearer (imsi,rnti,bearerId);
}


void
MmWaveHelper::SetEpcHelper (Ptr<EpcHelper> epcHelper)
{
  m_epcHelper = epcHelper;
}

class MmWaveDrbActivator : public SimpleRefCount<MmWaveDrbActivator>
{
public:
  MmWaveDrbActivator (Ptr<NetDevice> ueDevice, EpsBearer bearer);
  static void ActivateCallback (Ptr<MmWaveDrbActivator> a, std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti);
  void ActivateDrb (uint64_t imsi, uint16_t cellId, uint16_t rnti);
private:
  bool m_active;
  Ptr<NetDevice> m_ueDevice;
  EpsBearer m_bearer;
  uint64_t m_imsi;
};

MmWaveDrbActivator::MmWaveDrbActivator (Ptr<NetDevice> ueDevice, EpsBearer bearer)
  : m_active (false),
  m_ueDevice (ueDevice),
  m_bearer (bearer),
  m_imsi (m_ueDevice->GetObject< MmWaveUeNetDevice> ()->GetImsi ())
{
}

void
MmWaveDrbActivator::ActivateCallback (Ptr<MmWaveDrbActivator> a, std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  NS_LOG_FUNCTION (a << context << imsi << cellId << rnti);
  a->ActivateDrb (imsi, cellId, rnti);
}

void
MmWaveDrbActivator::ActivateDrb (uint64_t imsi, uint16_t cellId, uint16_t rnti)
{

  NS_LOG_FUNCTION (this << imsi << cellId << rnti << m_active);
  if ((!m_active) && (imsi == m_imsi))
    {
      Ptr<LteUeRrc> ueRrc = m_ueDevice->GetObject<MmWaveUeNetDevice> ()->GetRrc ();
      NS_ASSERT (ueRrc->GetState () == LteUeRrc::CONNECTED_NORMALLY);
      uint16_t rnti = ueRrc->GetRnti ();
      Ptr<MmWaveEnbNetDevice> enbLteDevice = m_ueDevice->GetObject<MmWaveUeNetDevice> ()->GetTargetEnb ();
      Ptr<LteEnbRrc> enbRrc = enbLteDevice->GetObject<MmWaveEnbNetDevice> ()->GetRrc ();
      NS_ASSERT (ueRrc->GetCellId () == enbLteDevice->GetCellId ());
      Ptr<UeManager> ueManager = enbRrc->GetUeManager (rnti);
      NS_ASSERT (ueManager->GetState () == UeManager::CONNECTED_NORMALLY
                 || ueManager->GetState () == UeManager::CONNECTION_RECONFIGURATION);
      EpcEnbS1SapUser::DataRadioBearerSetupRequestParameters params;
      params.rnti = rnti;
      params.bearer = m_bearer;
      params.bearerId = 0;
      params.gtpTeid = 0;   // don't care
      enbRrc->GetS1SapUser ()->DataRadioBearerSetupRequest (params);
      m_active = true;
    }
}
void
MmWaveHelper::ActivateDataRadioBearer (NetDeviceContainer ueDevices, EpsBearer bearer)
{
  NS_LOG_FUNCTION (this);
  for (NetDeviceContainer::Iterator i = ueDevices.Begin (); i != ueDevices.End (); ++i)
    {
      ActivateDataRadioBearer (*i, bearer);
    }
}
void
MmWaveHelper::ActivateDataRadioBearer (Ptr<NetDevice> ueDevice, EpsBearer bearer)
{
  NS_LOG_FUNCTION (this << ueDevice);
  NS_ASSERT_MSG (m_epcHelper == 0, "this method must not be used when the EPC is being used");

  // Normally it is the EPC that takes care of activating DRBs
  // when the UE gets connected. When the EPC is not used, we achieve
  // the same behavior by hooking a dedicated DRB activation function
  // to the Enb RRC Connection Established trace source


  Ptr<MmWaveEnbNetDevice> enbmmWaveDevice = ueDevice->GetObject<MmWaveUeNetDevice> ()->GetTargetEnb ();

  std::ostringstream path;
  path << "/NodeList/" << enbmmWaveDevice->GetNode ()->GetId ()
       << "/DeviceList/" << enbmmWaveDevice->GetIfIndex ()
       << "/LteEnbRrc/ConnectionEstablished";
  Ptr<MmWaveDrbActivator> arg = Create<MmWaveDrbActivator> (ueDevice, bearer);
  Config::Connect (path.str (), MakeBoundCallback (&MmWaveDrbActivator::ActivateCallback, arg));
}


void
MmWaveHelper::EnableTraces (void)
{
  EnableDlPhyTrace ();
  EnableUlPhyTrace ();
  //EnableEnbPacketCountTrace ();
  //EnableUePacketCountTrace ();
  //EnableTransportBlockTrace ();
  EnableRlcTraces ();
  EnablePdcpTraces ();
  EnableEnbPhyCtrlMsgsTraces ();
  EnableUePhyCtrlMsgsTraces ();
  EnableEnbMacCtrlMsgsTraces ();
  EnableUeMacCtrlMsgsTraces ();
}

void
MmWaveHelper::EnableDlPhyTrace (void)
{
  //NS_LOG_FUNCTION_NOARGS ();
  //Config::Connect ("/NodeList/*/DeviceList/*/MmWaveUePhy/ReportCurrentCellRsrpSinr",
  //  MakeBoundCallback (&MmWavePhyRxTrace::ReportCurrentCellRsrpSinrCallback, m_phyStats));

  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/MmWaveUePhy/SpectrumPhy/RxPacketTraceUe",
                   MakeBoundCallback (&MmWavePhyRxTrace::RxPacketTraceUeCallback, m_phyStats));
}

void
MmWaveHelper::EnableEnbPhyCtrlMsgsTraces (void)
{
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMap/*/MmWaveEnbPhy/EnbPhyRxedCtrlMsgsTrace",
                   MakeBoundCallback (&MmWavePhyRxTrace::RxedEnbPhyCtrlMsgsCallback, m_phyStats));
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMap/*/MmWaveEnbPhy/EnbPhyTxedCtrlMsgsTrace",
                   MakeBoundCallback (&MmWavePhyRxTrace::TxedEnbPhyCtrlMsgsCallback, m_phyStats));
}

void
MmWaveHelper::EnableEnbMacCtrlMsgsTraces (void)
{
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMap/*/MmWaveEnbMac/EnbMacRxedCtrlMsgsTrace",
                   MakeBoundCallback (&MmwaveMacRxTrace::RxedEnbMacCtrlMsgsCallback, m_macStats));

  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMap/*/MmWaveEnbMac/EnbMacTxedCtrlMsgsTrace",
                   MakeBoundCallback (&MmwaveMacRxTrace::TxedEnbMacCtrlMsgsCallback, m_macStats));
}

void
MmWaveHelper::EnableUePhyCtrlMsgsTraces (void)
{
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/MmWaveUePhy/UePhyRxedCtrlMsgsTrace",
                   MakeBoundCallback (&MmWavePhyRxTrace::RxedUePhyCtrlMsgsCallback, m_phyStats));
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/MmWaveUePhy/UePhyTxedCtrlMsgsTrace",
                   MakeBoundCallback (&MmWavePhyRxTrace::TxedUePhyCtrlMsgsCallback, m_phyStats));
}

void
MmWaveHelper::EnableUeMacCtrlMsgsTraces (void)
{
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/MmWaveUeMac/UeMacRxedCtrlMsgsTrace",
                   MakeBoundCallback (&MmwaveMacRxTrace::RxedUeMacCtrlMsgsCallback, m_macStats));
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/MmWaveUeMac/UeMacTxedCtrlMsgsTrace",
                   MakeBoundCallback (&MmwaveMacRxTrace::TxedUeMacCtrlMsgsCallback, m_macStats));
}

void
MmWaveHelper::EnableUlPhyTrace (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMap/*/MmWaveEnbPhy/SpectrumPhy/RxPacketTraceEnb",
                   MakeBoundCallback (&MmWavePhyRxTrace::RxPacketTraceEnbCallback, m_phyStats));
}

void
MmWaveHelper::EnableEnbPacketCountTrace ()
{
  NS_LOG_FUNCTION_NOARGS ();
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMap/*/MmWaveEnbPhy/SpectrumPhy/ReportEnbTxRxPacketCount",
                   MakeBoundCallback (&MmWavePhyRxTrace::ReportPacketCountEnbCallback, m_phyStats));

}

void
MmWaveHelper::EnableUePacketCountTrace ()
{
  NS_LOG_FUNCTION_NOARGS ();
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/MmWaveUePhy/SpectrumPhy/ReportUeTxRxPacketCount",
                   MakeBoundCallback (&MmWavePhyRxTrace::ReportPacketCountUeCallback, m_phyStats));

}

void
MmWaveHelper::EnableTransportBlockTrace ()
{
  NS_LOG_FUNCTION_NOARGS ();
  Config::Connect ("/NodeList/*/DeviceList/*/ComponentCarrierMapUe/*/MmWaveUePhy/ReportDownlinkTbSize",
                   MakeBoundCallback (&MmWavePhyRxTrace::ReportDownLinkTBSize, m_phyStats));
}


void
MmWaveHelper::EnableRlcTraces (void)
{
  NS_ASSERT_MSG (m_rlcStats == 0, "please make sure that MmWaveHelper::EnableRlcTraces is called at most once");
  m_rlcStats = CreateObject<MmWaveBearerStatsCalculator> ("RLC");
  m_radioBearerStatsConnector.EnableRlcStats (m_rlcStats);
}

Ptr<MmWaveBearerStatsCalculator>
MmWaveHelper::GetRlcStats (void)
{
  return m_rlcStats;
}

void
MmWaveHelper::EnablePdcpTraces (void)
{
  NS_ASSERT_MSG (m_pdcpStats == 0, "please make sure that MmWaveHelper::EnablePdcpTraces is called at most once");
  m_pdcpStats = CreateObject<MmWaveBearerStatsCalculator> ("PDCP");
  m_radioBearerStatsConnector.EnablePdcpStats (m_pdcpStats);
}

Ptr<MmWaveBearerStatsCalculator>
MmWaveHelper::GetPdcpStats (void)
{
  return m_pdcpStats;
}

BandwidthPartRepresentation::BandwidthPartRepresentation(uint32_t id,
                                                         const Ptr<MmWavePhyMacCommon> &phyMacCommon,
                                                         const Ptr<SpectrumChannel> &channel,
                                                         const Ptr<PropagationLossModel> &propagation,
                                                         const Ptr<MmWave3gppChannel> & spectrumPropagation)
  : m_id (id),
    m_phyMacCommon (phyMacCommon),
    m_channel (channel),
    m_propagation (propagation),
    m_3gppChannel (spectrumPropagation)
{
  NS_LOG_FUNCTION (this);
}

BandwidthPartRepresentation::BandwidthPartRepresentation (const BandwidthPartRepresentation &o)
{
  NS_LOG_FUNCTION (this);
  m_id = o.m_id;
  m_phyMacCommon = o.m_phyMacCommon;
  m_channel = o.m_channel;
  m_propagation = o.m_propagation;
  m_3gppChannel = o.m_3gppChannel;
  m_gnbChannelAccessManagerType = o.m_gnbChannelAccessManagerType;
  m_ueChannelAccessManagerType = o.m_ueChannelAccessManagerType;
}

BandwidthPartRepresentation::~BandwidthPartRepresentation()
{
  NS_LOG_FUNCTION (this);
}

BandwidthPartRepresentation &
BandwidthPartRepresentation::operator=(const BandwidthPartRepresentation &o)
{
  m_id = o.m_id;
  m_phyMacCommon = o.m_phyMacCommon;
  m_channel = o.m_channel;
  m_propagation = o.m_propagation;
  m_3gppChannel = o.m_3gppChannel;
  m_gnbChannelAccessManagerType = o.m_gnbChannelAccessManagerType;
  m_ueChannelAccessManagerType = o.m_ueChannelAccessManagerType;
  return *this;
}



static bool CarrierFrequencyCompare (const ComponentCarrierInfo & lhs,
			     const ComponentCarrierInfo & rhs)
{
  return lhs.m_centralFrequency < rhs.m_centralFrequency;
}

static bool BwpFrequencyCompare (const ComponentCarrierBandwidthPartElement & lhs,
			 const ComponentCarrierBandwidthPartElement & rhs)
{
  return lhs.m_centralFrequency < rhs.m_centralFrequency;
}

static bool BwpIdCompare (const ComponentCarrierBandwidthPartElement & lhs,
		  const ComponentCarrierBandwidthPartElement & rhs)
{
  return lhs.m_bwpId < rhs.m_bwpId;
}



ComponentCarrierBandwidthPartCreator::ComponentCarrierBandwidthPartCreator ()
{
  NS_LOG_FUNCTION (this);
}

ComponentCarrierBandwidthPartCreator::ComponentCarrierBandwidthPartCreator (uint8_t maxNumBands)
{
  NS_LOG_FUNCTION (this);
  m_maxBands = maxNumBands;
}

ComponentCarrierBandwidthPartCreator::~ComponentCarrierBandwidthPartCreator ()
{
  NS_LOG_FUNCTION (this);
}

ComponentCarrierBandwidthPartCreator&
ComponentCarrierBandwidthPartCreator::operator= (const ns3::ComponentCarrierBandwidthPartCreator& o)
{
  m_id = o.m_id;
  m_maxBands = o.m_maxBands;
  m_bands = o.m_bands;
  m_numBands = o.m_numBands;
  m_numBwps = o.m_numBwps;
  m_numCcs = o.m_numCcs;
  return *this;
}

void
ComponentCarrierInfo::AddBwp (const ComponentCarrierBandwidthPartElement & bwp)
{
  NS_ABORT_MSG_IF (m_numBwps >= 4,"Maximum number of BWPs reached (4)");
  m_bwp.push_back(bwp);
  ++m_numBwps;
}


void
OperationBandInfo::AddCc (const ComponentCarrierInfo & cc)
{
  NS_ABORT_MSG_IF (m_numCarriers >= MAX_CC_INTRA_BAND,"The maximum number of CCs in the band was reached");
  m_cc.push_back(cc);
  ++m_numCarriers;
}



void
ComponentCarrierBandwidthPartCreator::CreateOperationBandContiguousCc (double centralFrequency, uint32_t operationBandwidth, uint8_t numCCs)
{

  NS_ABORT_MSG_IF (m_numBands == m_maxBands,"Maximum number of operation bands reached"<<(uint16_t)m_maxBands);

  OperationBandInfo band;
  band.m_centralFrequency = centralFrequency;
  band.m_bandwidth = operationBandwidth;
  band.m_lowerFrequency = centralFrequency - (double)operationBandwidth/2;
  band.m_higherFrequency = centralFrequency + (double)operationBandwidth/2;
  band.m_numCarriers = numCCs;
  band.m_contiguousCc = CONTIGUOUS;

  uint8_t numerology = 2;
  uint32_t maxCcBandwidth = 198e6;

  if (centralFrequency > 6e9)
    {
      numerology = 3;
      maxCcBandwidth = 396e6;
    }

  double ccBandwidth = std::min((double)maxCcBandwidth,(double)operationBandwidth/numCCs);

  uint16_t numRBs = ccBandwidth/(12*15e3*std::pow(2,numerology));
  NS_ABORT_MSG_IF(numRBs < 24, "Carrier bandwidth is below the minimum number of RBs (24)");
  NS_ABORT_MSG_IF(numRBs > 275, "Carrier bandwidth is larger than the maximum number of RBs (275)");

  for (uint8_t c = 0; c < numCCs; c++)
    {
      ComponentCarrierInfo cc;
      cc.m_centralFrequency = band.m_lowerFrequency+c*ccBandwidth+ccBandwidth/2;
      cc.m_lowerFrequency = band.m_lowerFrequency+c*ccBandwidth;
      cc.m_higherFrequency = band.m_lowerFrequency+(c+1)*ccBandwidth-1;
      cc.m_bandwidth = ccBandwidth;
      cc.m_numBwps = 1;
      cc.m_activeBwp = m_numBwps;
      ComponentCarrierBandwidthPartElement bwp;
      bwp.m_numerology = numerology;
      bwp.m_centralFrequency = cc.m_centralFrequency;
      bwp.m_lowerFrequency = cc.m_lowerFrequency;
      bwp.m_higherFrequency = cc.m_higherFrequency;
      bwp.m_bandwidth = cc.m_bandwidth;
      cc.m_bwp.push_back(bwp);
      m_numBwps++;
      band.m_cc.push_back(cc);
      m_numCcs++;
    }
  m_bands.push_back(band);
  m_numBands++;

}


OperationBandInfo
ComponentCarrierBandwidthPartCreator::CreateOperationBand (double centralFrequency, uint32_t operationBandwidth)
{
  OperationBandInfo band;
  band.m_centralFrequency = centralFrequency;
  band.m_bandwidth = operationBandwidth;
  return band;

}

OperationBandInfo
ComponentCarrierBandwidthPartCreator::CreateOperationBand (double centralFrequency, uint32_t operationBandwidth, std::vector<ComponentCarrierInfo> &ccs)
{

  NS_ABORT_MSG_IF(ccs.empty(),"No CC definition provided");

  ContiguousMode contiguous = CONTIGUOUS;
  uint8_t numCcs = ccs.size();
  NS_ABORT_MSG_IF(numCcs > MAX_CC_INTRA_BAND,"The number of CCs is larger than the maximum");

  // Checks if CCs overlap and are contiguous. CCs must be ordered in increasing central frequency value
  // Uncomment the code below if you created the CC vector out of the API and needs reordering
  std::sort(ccs.begin(),ccs.end(),CarrierFrequencyCompare);
  uint8_t c=0;
  while (c < numCcs-1)
    {
      if ((double)ccs.at(c+1).m_lowerFrequency - (double)ccs.at(c).m_higherFrequency  < 0)
        {
          NS_ABORT_MSG("CCs overlap");
        }
      if (ccs.at(c+1).m_lowerFrequency - ccs.at(c).m_higherFrequency > 1)
        {
          contiguous = NON_CONTIGUOUS;
        }
      ++c;
    }

  // Check if each CC has BWP configuration and validate it
  for (auto & cc : ccs)
    {
      CheckBwpsInCc(cc);
    }

  // At this point, CC configuration is valid. Create the operation band.
  OperationBandInfo band;
  band.m_centralFrequency = centralFrequency;
  band.m_bandwidth = operationBandwidth;
  band.m_lowerFrequency = centralFrequency - operationBandwidth/2;
  band.m_higherFrequency = centralFrequency + operationBandwidth/2;
  band.m_numCarriers = numCcs;
  band.m_contiguousCc = contiguous;
  band.m_cc = ccs;
  return band;

}


void
ComponentCarrierBandwidthPartCreator::AddOperationBand (const OperationBandInfo &band)
{
  NS_ABORT_MSG_IF(m_numBands >= m_maxBands,"Maximum number of operation bands reached");

  m_bands.push_back(band);
  ++m_numBands;
  m_numCcs += band.m_numCarriers;
  for (const auto & cc : band.m_cc)
    {
      m_numBwps += cc.m_bwp.size();
    }

}

void
ComponentCarrierBandwidthPartCreator::ValidateOperationBand (OperationBandInfo &band)
{

  NS_ABORT_MSG_IF(band.m_cc.empty(),"No CC information provided");
  NS_ABORT_MSG_IF(band.m_numCarriers != band.m_cc.size(),"The declared number of intra-band CCs does not match the number of configured CCs");

  uint8_t numCcs = band.m_cc.size();
  ContiguousMode contiguous = CONTIGUOUS;

  // Sort CC by ascending central frequency value
  std::sort(band.m_cc.begin(),band.m_cc.end(),CarrierFrequencyCompare);

  // Loop checks if CCs are overlap and contiguous or not
  uint8_t c=0;
  while (c < numCcs-1)
    {
      if ((double)band.m_cc.at(c+1).m_lowerFrequency - (double)band.m_cc.at(c).m_higherFrequency < 0)
        {
          NS_ABORT_MSG("CCs overlap");
        }
      if (band.m_cc.at(c+1).m_lowerFrequency - band.m_cc.at(c).m_higherFrequency > 1)  //TODO: Consider changing the frequency separation value depending on the SCS
	{
	  contiguous = NON_CONTIGUOUS;
	}
      ++c;
    }

  band.m_contiguousCc = contiguous;

  // Check if each CC has BWP configuration and validate them
//  for (uint8_t c = 0; c < numCcs; c++)
  for (auto & cc : band.m_cc)
    {
      CheckBwpsInCc(cc);
    }
}


void
ComponentCarrierBandwidthPartCreator::CheckBwpsInCc (ComponentCarrierInfo &cc)
{
  // First check: number of BWP shall not be larger than 4
  uint8_t numBwps = cc.m_bwp.size();

  NS_ABORT_MSG_IF (numBwps > 4 || numBwps < 1,"The number of BWPs exceeds the maximum value (4)");

  // Second check: BWP shall not exceed CC limits and the sum of BWPs cannot be larger than the CC bandwidth
  std::sort(cc.m_bwp.begin(), cc.m_bwp.end(), BwpFrequencyCompare);
  uint32_t totalBandwidth = 0;
  bool activeFound = false;
  for (const auto & a : cc.m_bwp)
    {
      totalBandwidth += a.m_bandwidth;
      if (a.m_higherFrequency > cc.m_higherFrequency || a.m_lowerFrequency < cc.m_lowerFrequency)
        {
          NS_ABORT_MSG("BWP part is out of the CC");
        }
      if (a.m_bwpId == cc.m_activeBwp)
        {
          activeFound = true;
        }
    }
  NS_ABORT_MSG_IF (totalBandwidth > cc.m_bandwidth,"Aggregated BWP is larger than carrier bandwidth");

  // Third check: the active BWP id is in the CC description
  NS_ABORT_MSG_IF (activeFound==false,"The active BWP id was not found in the CC");


  // Fourth check: BWPs shall not overlap in frequency
  for (uint8_t a = 0; a < numBwps-1; a++)
    {
      if (cc.m_bwp.at(a).m_higherFrequency > cc.m_bwp.at(a+1).m_lowerFrequency)
        {
          NS_ABORT_MSG("BWPs shall not overlap");
        }
    }

  // Fifth check: BWP ids are not repeated
  std::sort(cc.m_bwp.begin(), cc.m_bwp.end(), BwpIdCompare);
  for (uint8_t i = 0; i < numBwps-1; i++)
    {
      if (cc.m_bwp.at(i).m_bwpId == cc.m_bwp.at(i+1).m_bwpId)
        {
          NS_ABORT_MSG("Repeated BWP id");
        }
    }

}


void
ComponentCarrierBandwidthPartCreator::ValidateCaBwpConfiguration ()
{
  // First: Number of band must be consistent
  NS_ABORT_MSG_IF(m_numBands != m_bands.size(),"The number of bands does not match the number of bands created");

  // Second: Number of bands below the maximum number
  NS_ABORT_MSG_IF(m_numBands > m_maxBands,"The number of bands is larger than the maximum number");

  uint16_t numAggrCcs = 0;
  uint8_t numPrimaryCcs = 0;
  for (auto & a : m_bands)
    {
      // Third: Check that the CC configuration is valid
      ValidateOperationBand(a);

      for (const auto & b : m_bands)
        {
	  // Fourth: Operation bands shall not overlap
          if (&a != &b && a.m_higherFrequency < b.m_lowerFrequency)
            {
              NS_ABORT_MSG("Bands shall not overlap");
            }
          for (const auto & cc : b.m_cc)
            {
              if (cc.m_primaryCc == PRIMARY)
        	{
        	  ++numPrimaryCcs;
        	}
            }
        }

      numAggrCcs += a.m_numCarriers;
    }

  // Fifth: Check that the number of the inter-band aggregated carriers is below the maximum value
  NS_ABORT_MSG_IF(numAggrCcs > MAX_CC_INTER_BAND,"The number of allowed aggregated CCs was exceeded");

  // Sixth: There must be one primary CC only
  NS_ABORT_MSG_IF(numPrimaryCcs != 1,"There must be one primary CC");
}


ContiguousMode
ComponentCarrierBandwidthPartCreator::GetCcContiguousnessState (OperationBandInfo &band, uint32_t freqSeparation)
{
  // Make sure there is more than 1 CC
  NS_ABORT_MSG_IF(band.m_numCarriers < 1,"There should be more than 1 CC to determine if they are contiguous");

  // Assume that CCs might not be ordered in an increasing central frequency value
  std::sort(band.m_cc.begin(), band.m_cc.end(), CarrierFrequencyCompare);

  for (uint8_t i = 0; i < band.m_numCarriers-1; i++)
    {
      if (band.m_cc.at(i).m_lowerFrequency - band.m_cc.at(i+1).m_higherFrequency > freqSeparation)
        {
          return NON_CONTIGUOUS;
        }
    }
  return CONTIGUOUS;
}


ComponentCarrierBandwidthPartElement
ComponentCarrierBandwidthPartCreator::GetActiveBwpInfo ()
{
  NS_ABORT_MSG_IF(m_bands.empty(),"No operation band information provided");

  for (const auto band : m_bands)
    {
      NS_ABORT_MSG_IF(band.m_cc.empty(),"Missing some CC information");
      for (const auto cc : band.m_cc)
	{
	  if (cc.m_primaryCc == PRIMARY)
	    {
	      NS_ABORT_MSG_IF(cc.m_bwp.empty(),"Missing some BWP information");
	      for (const auto bwp : cc.m_bwp)
		{
		  if (bwp.m_bwpId == cc.m_activeBwp)
		    {
		      return bwp;
		    }
		}
	    }
	}
    }

  NS_ABORT_MSG("No active BWP information found in the primary CC");

}



ComponentCarrierBandwidthPartElement
ComponentCarrierBandwidthPartCreator::GetActiveBwpInfo (uint8_t bandIndex, uint8_t ccIndex)
{
  NS_ABORT_MSG_IF(m_bands.empty(),"No operation band information provided");
  NS_ABORT_MSG_IF(bandIndex >= m_maxBands || bandIndex >= m_bands.size(), "Wrong operation band index");
//  NS_ABORT_MSG_IF(ccIndex > m_numCcs,"Wrong component carrier index");


  OperationBandInfo band = m_bands.at(bandIndex);
  NS_ABORT_MSG_IF(band.m_cc.empty(),"No carrier band information provided");
  NS_ABORT_MSG_IF(ccIndex > band.m_numCarriers-1 || ccIndex > band.m_cc.size()-1,"Wrong component carrier index");

  ComponentCarrierInfo cc = band.m_cc.at(ccIndex);
  ComponentCarrierBandwidthPartElement bwp;
  bool found = false;
  for (const auto & b : cc.m_bwp)
    {
      if(b.m_bwpId == cc.m_activeBwp)
        {
          found = true;
          bwp = b;
          break;
        }
    }
  NS_ABORT_MSG_IF(found==false,"Active BWP id is not found in the current CC");
  return bwp;

}

ComponentCarrierInfo
ComponentCarrierBandwidthPartCreator::GetComponentCarrier (uint8_t bandId, uint8_t ccId)
{
  NS_ABORT_MSG_IF(bandId >= m_numBands,"Wrong operation band id");
  NS_ABORT_MSG_IF(m_bands.at(bandId).m_numCarriers <= ccId, "CC index exceeds the number of defined CCs");
  return m_bands.at(bandId).m_cc.at(ccId);
}


uint32_t ComponentCarrierBandwidthPartCreator::GetAggregatedBandwidth ()
{
  uint32_t aBandwidth = 0;
  for (const auto & band : m_bands)
    {
      for (const auto & cc : band.m_cc)
	{
	  for (const auto & bwp : cc.m_bwp)
	    {
	      if (bwp.m_bwpId == cc.m_activeBwp)
		{
		  aBandwidth += bwp.m_bandwidth;
		}
	    }
	}
    }

  return aBandwidth;
}


uint32_t
ComponentCarrierBandwidthPartCreator::GetCarrierBandwidth (uint8_t bandId, uint8_t ccId)
{
  // There is at least one bwp
  ComponentCarrierBandwidthPartElement bwp = GetActiveBwpInfo(bandId, ccId);
  return bwp.m_bandwidth;
}


void
ComponentCarrierBandwidthPartCreator::ChangeActiveBwp (uint8_t bandId, uint8_t ccId, uint8_t activeBwpId)
{
  for (auto & band : m_bands)
    {
      if (band.m_bandId == bandId)
	{
	  for (auto & cc : band.m_cc)
	    {
	      if (cc.m_ccId == ccId)
		{
		  for (const auto & bwp : cc.m_bwp)
		    {
		      if (bwp.m_bwpId == activeBwpId)
			{
			  cc.m_activeBwp = activeBwpId;
			  return;
			}
		    }
		}
	    }
	}

    }
  NS_ABORT_MSG("Could not change the active BWP due to wrong request");
}



} // namespace ns3

