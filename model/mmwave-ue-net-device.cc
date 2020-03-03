/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 *   Copyright (c) 2019 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
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
 */

#include "mmwave-ue-net-device.h"
#include "component-carrier-mmwave-ue.h"
#include "mmwave-ue-mac.h"
#include "mmwave-ue-phy.h"
#include "mmwave-enb-net-device.h"
#include "bwp-manager-ue.h"
#include <ns3/lte-ue-rrc.h>
#include <ns3/epc-ue-nas.h>
#include <ns3/lte-ue-component-carrier-manager.h>
#include <ns3/ipv4-l3-protocol.h>
#include <ns3/object-map.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MmWaveUeNetDevice");

NS_OBJECT_ENSURE_REGISTERED (MmWaveUeNetDevice);

TypeId
MmWaveUeNetDevice::GetTypeId (void)
{
  static TypeId
    tid =  TypeId ("ns3::MmWaveUeNetDevice")
    .SetParent<MmWaveNetDevice> ()
    .AddConstructor<MmWaveUeNetDevice> ()
    .AddAttribute ("EpcUeNas",
                   "The NAS associated to this UeNetDevice",
                   PointerValue (),
                   MakePointerAccessor (&MmWaveUeNetDevice::m_nas),
                   MakePointerChecker <EpcUeNas> ())
    .AddAttribute ("mmWaveUeRrc",
                   "The RRC associated to this UeNetDevice",
                   PointerValue (),
                   MakePointerAccessor (&MmWaveUeNetDevice::m_rrc),
                   MakePointerChecker <LteUeRrc> ())
    .AddAttribute ("Imsi",
                   "International Mobile Subscriber Identity assigned to this UE",
                   UintegerValue (0),
                   MakeUintegerAccessor (&MmWaveUeNetDevice::m_imsi),
                   MakeUintegerChecker<uint64_t> ())
    .AddAttribute ("LteUeRrc",
                   "The RRC layer associated with the ENB",
                   PointerValue (),
                   MakePointerAccessor (&MmWaveUeNetDevice::m_rrc),
                   MakePointerChecker <LteUeRrc> ())
    .AddAttribute ("LteUeComponentCarrierManager",
                   "The ComponentCarrierManager associated to this UeNetDevice",
                   PointerValue (),
                   MakePointerAccessor (&MmWaveUeNetDevice::m_componentCarrierManager),
                   MakePointerChecker <LteUeComponentCarrierManager> ())
    .AddAttribute ("ComponentCarrierMapUe", "List of all component Carrier.",
                   ObjectMapValue (),
                   MakeObjectMapAccessor (&MmWaveUeNetDevice::m_ccMap),
                   MakeObjectMapChecker<ComponentCarrierMmWaveUe> ())
  ;
  return tid;
}

MmWaveUeNetDevice::MmWaveUeNetDevice (void)
  : m_isConstructed (false)
{
  NS_LOG_FUNCTION (this);
}

MmWaveUeNetDevice::~MmWaveUeNetDevice (void)
{

}

void
MmWaveUeNetDevice::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  m_isConstructed = true;
  UpdateConfig ();

  std::map< uint8_t, Ptr<ComponentCarrierMmWaveUe> >::iterator it;
  for (it = m_ccMap.begin (); it != m_ccMap.end (); ++it)
    {
      it->second->GetMac ()->Initialize ();
    }
  m_rrc->Initialize ();
}
void
MmWaveUeNetDevice::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  m_rrc->Dispose ();
}

std::map < uint8_t, Ptr<ComponentCarrierMmWaveUe> >
MmWaveUeNetDevice::GetCcMap ()
{
  NS_LOG_FUNCTION (this);
  return m_ccMap;
}

uint32_t
MmWaveUeNetDevice::GetCcMapSize() const
{
  NS_LOG_FUNCTION (this);
  return m_ccMap.size ();
}

void
MmWaveUeNetDevice::EnqueueDlHarqFeedback (const DlHarqInfo &m) const
{
  NS_LOG_FUNCTION (this);

  auto ccManager = DynamicCast<BwpManagerUe> (m_componentCarrierManager);
  NS_ASSERT (ccManager != nullptr);
  uint8_t index = ccManager->RouteDlHarqFeedback (m);
  m_ccMap.at (index)->GetPhy ()->EnqueueDlHarqFeedback (m);
}

void
MmWaveUeNetDevice::SetCcMap (std::map< uint8_t, Ptr<ComponentCarrierMmWaveUe> > ccm)
{
  NS_LOG_FUNCTION (this);
  m_ccMap = ccm;
}

uint32_t
MmWaveUeNetDevice::GetCsgId () const
{
  NS_LOG_FUNCTION (this);
  return m_csgId;
}

void
MmWaveUeNetDevice::SetCsgId (uint32_t csgId)
{
  NS_LOG_FUNCTION (this << csgId);
  m_csgId = csgId;
  UpdateConfig (); // propagate the change down to NAS and RRC
}

void
MmWaveUeNetDevice::UpdateConfig (void)
{
  NS_LOG_FUNCTION (this);

  if (m_isConstructed)
    {
      NS_LOG_LOGIC (this << " Updating configuration: IMSI " << m_imsi
                         << " CSG ID " << m_csgId);
      m_nas->SetImsi (m_imsi);
      m_rrc->SetImsi (m_imsi);
      m_nas->SetCsgId (m_csgId); // this also handles propagation to RRC
    }
  else
    {
      /*
       * NAS and RRC instances are not be ready yet, so do nothing now and
       * expect ``DoInitialize`` to re-invoke this function.
       */
    }
}

bool
MmWaveUeNetDevice::DoSend (Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
  NS_LOG_FUNCTION (this << dest << protocolNumber);
  if (protocolNumber != Ipv4L3Protocol::PROT_NUMBER)
    {
      NS_LOG_INFO ("unsupported protocol " << protocolNumber << ", only IPv4 is supported");
      return false;
    }
  return m_nas->Send (packet, protocolNumber);
}

Ptr<MmWaveUePhy>
MmWaveUeNetDevice::GetPhy (uint8_t index) const
{
  NS_LOG_FUNCTION (this);
  return m_ccMap.at (index)->GetPhy ();
}

Ptr<MmWavePhy>
MmWaveUeNetDevice::GetPhyOnCenterFreq (double centerFrequency) const
{
  NS_LOG_FUNCTION (this);

  for (auto i:m_ccMap)
    {
      if (i.second->GetDlEarfcn())
        {
          return i.second->GetPhy();
        }
    }
  NS_LOG_INFO ("PHY instance does not exist for center frequency:"<<centerFrequency);
  return nullptr;
}


Ptr<LteUeComponentCarrierManager>
MmWaveUeNetDevice::GetComponentCarrierManager (void) const
{
  NS_LOG_FUNCTION (this);
  return m_componentCarrierManager;
}

Ptr<EpcUeNas>
MmWaveUeNetDevice::GetNas (void) const
{
  NS_LOG_FUNCTION (this);
  return m_nas;
}


Ptr<LteUeRrc>
MmWaveUeNetDevice::GetRrc (void) const
{
  NS_LOG_FUNCTION (this);
  return m_rrc;
}

uint64_t
MmWaveUeNetDevice::GetImsi () const
{
  NS_LOG_FUNCTION (this);
  return m_imsi;
}

uint16_t
MmWaveUeNetDevice::GetEarfcn () const
{
  NS_LOG_FUNCTION (this);
  return m_earfcn;
}

void
MmWaveUeNetDevice::SetEarfcn (uint16_t earfcn)
{
  NS_LOG_FUNCTION (this);
  m_earfcn = earfcn;
}

void
MmWaveUeNetDevice::SetTargetEnb (Ptr<MmWaveEnbNetDevice> enb)
{
  NS_LOG_FUNCTION (this);
  m_targetEnb = enb;
}

Ptr<MmWaveEnbNetDevice>
MmWaveUeNetDevice::GetTargetEnb (void)
{
  NS_LOG_FUNCTION (this);
  return m_targetEnb;
}

}
