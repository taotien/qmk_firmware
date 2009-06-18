/*
             LUFA Library
     Copyright (C) Dean Camera, 2009.
              
  dean [at] fourwalledcubicle [dot] com
      www.fourwalledcubicle.com
*/

/*
  Copyright 2009  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, and distribute this software
  and its documentation for any purpose and without fee is hereby
  granted, provided that the above copyright notice appear in all
  copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaim all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

#include "../../HighLevel/USBMode.h"
#if defined(USB_CAN_BE_DEVICE)

#define  INCLUDE_FROM_RNDIS_CLASS_DEVICE_C
#include "RNDIS.h"

static const uint32_t PROGMEM AdapterSupportedOIDList[]  =
	{
		OID_GEN_SUPPORTED_LIST,
		OID_GEN_PHYSICAL_MEDIUM,
		OID_GEN_HARDWARE_STATUS,
		OID_GEN_MEDIA_SUPPORTED,
		OID_GEN_MEDIA_IN_USE,
		OID_GEN_MAXIMUM_FRAME_SIZE,
		OID_GEN_MAXIMUM_TOTAL_SIZE,
		OID_GEN_LINK_SPEED,
		OID_GEN_TRANSMIT_BLOCK_SIZE,
		OID_GEN_RECEIVE_BLOCK_SIZE,
		OID_GEN_VENDOR_ID,
		OID_GEN_VENDOR_DESCRIPTION,
		OID_GEN_CURRENT_PACKET_FILTER,
		OID_GEN_MAXIMUM_TOTAL_SIZE,
		OID_GEN_MEDIA_CONNECT_STATUS,
		OID_GEN_XMIT_OK,
		OID_GEN_RCV_OK,
		OID_GEN_XMIT_ERROR,
		OID_GEN_RCV_ERROR,
		OID_GEN_RCV_NO_BUFFER,
		OID_802_3_PERMANENT_ADDRESS,
		OID_802_3_CURRENT_ADDRESS,
		OID_802_3_MULTICAST_LIST,
		OID_802_3_MAXIMUM_LIST_SIZE,
		OID_802_3_RCV_ERROR_ALIGNMENT,
		OID_802_3_XMIT_ONE_COLLISION,
		OID_802_3_XMIT_MORE_COLLISIONS,
	};

void RNDIS_Device_ProcessControlPacket(USB_ClassInfo_RNDIS_Device_t* RNDISInterfaceInfo)
{
	if (!(Endpoint_IsSETUPReceived()))
	  return;
	  
	if (USB_ControlRequest.wIndex != RNDISInterfaceInfo->Config.ControlInterfaceNumber)
	  return;

	switch (USB_ControlRequest.bRequest)
	{
		case REQ_SendEncapsulatedCommand:
			if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();

				Endpoint_Read_Control_Stream_LE(RNDISInterfaceInfo->State.RNDISMessageBuffer, USB_ControlRequest.wLength);
				Endpoint_ClearIN();

				RNDIS_Device_ProcessRNDISControlMessage(RNDISInterfaceInfo);
			}
			
			break;
		case REQ_GetEncapsulatedResponse:
			if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();

				RNDIS_Message_Header_t* MessageHeader = (RNDIS_Message_Header_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;

				if (!(MessageHeader->MessageLength))
				{
					RNDISInterfaceInfo->State.RNDISMessageBuffer[0] = 0;
					MessageHeader->MessageLength = 1;
				}

				Endpoint_Write_Control_Stream_LE(RNDISInterfaceInfo->State.RNDISMessageBuffer, MessageHeader->MessageLength);				
				Endpoint_ClearOUT();

				MessageHeader->MessageLength = 0;
			}
	
			break;
	}
}

bool RNDIS_Device_ConfigureEndpoints(USB_ClassInfo_RNDIS_Device_t* RNDISInterfaceInfo)
{
	if (!(Endpoint_ConfigureEndpoint(RNDISInterfaceInfo->Config.DataINEndpointNumber, EP_TYPE_BULK,
							         ENDPOINT_DIR_IN, RNDISInterfaceInfo->Config.DataINEndpointSize,
							         ENDPOINT_BANK_SINGLE)))
	{
		return false;
	}

	if (!(Endpoint_ConfigureEndpoint(RNDISInterfaceInfo->Config.DataOUTEndpointNumber, EP_TYPE_BULK,
	                                 ENDPOINT_DIR_OUT, RNDISInterfaceInfo->Config.DataOUTEndpointSize,
	                                 ENDPOINT_BANK_SINGLE)))
	{
		return false;
	}

	if (!(Endpoint_ConfigureEndpoint(RNDISInterfaceInfo->Config.NotificationEndpointNumber, EP_TYPE_INTERRUPT,
	                                 ENDPOINT_DIR_IN, RNDISInterfaceInfo->Config.NotificationEndpointSize,
	                                 ENDPOINT_BANK_SINGLE)))
	{
		return false;
	}

	return true;
}

void RNDIS_Device_USBTask(USB_ClassInfo_RNDIS_Device_t* RNDISInterfaceInfo)
{
	if (!(USB_IsConnected))
	  return;

	RNDIS_Message_Header_t* MessageHeader = (RNDIS_Message_Header_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;

	Endpoint_SelectEndpoint(RNDISInterfaceInfo->Config.NotificationEndpointNumber);

	if (Endpoint_IsINReady() && RNDISInterfaceInfo->State.ResponseReady)
	{
		USB_Request_Header_t Notification = (USB_Request_Header_t)
			{
				.bmRequestType = (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE),
				.bRequest      = NOTIF_ResponseAvailable,
				.wValue        = 0,
				.wIndex        = 0,
				.wLength       = 0,
			};
		
		Endpoint_Write_Stream_LE(&Notification, sizeof(Notification), NO_STREAM_CALLBACK);

		Endpoint_ClearIN();

		RNDISInterfaceInfo->State.ResponseReady = false;
	}
	
	if ((RNDISInterfaceInfo->State.CurrRNDISState == RNDIS_Data_Initialized) && !(MessageHeader->MessageLength))
	{
		RNDIS_PACKET_MSG_t RNDISPacketHeader;

		Endpoint_SelectEndpoint(RNDISInterfaceInfo->Config.DataOUTEndpointNumber);

		if (Endpoint_IsOUTReceived() && !(RNDISInterfaceInfo->State.FrameIN.FrameInBuffer))
		{
			Endpoint_Read_Stream_LE(&RNDISPacketHeader, sizeof(RNDIS_PACKET_MSG_t), NO_STREAM_CALLBACK);

			if (RNDISPacketHeader.DataLength > ETHERNET_FRAME_SIZE_MAX)
			{
				Endpoint_StallTransaction();
				return;
			}
			
			Endpoint_Read_Stream_LE(RNDISInterfaceInfo->State.FrameIN.FrameData, RNDISPacketHeader.DataLength, NO_STREAM_CALLBACK);

			Endpoint_ClearOUT();
			
			RNDISInterfaceInfo->State.FrameIN.FrameLength = RNDISPacketHeader.DataLength;

			RNDISInterfaceInfo->State.FrameIN.FrameInBuffer = true;
		}
		
		Endpoint_SelectEndpoint(RNDISInterfaceInfo->Config.DataINEndpointNumber);
		
		if (Endpoint_IsINReady() && RNDISInterfaceInfo->State.FrameOUT.FrameInBuffer)
		{
			memset(&RNDISPacketHeader, 0, sizeof(RNDIS_PACKET_MSG_t));

			RNDISPacketHeader.MessageType   = REMOTE_NDIS_PACKET_MSG;
			RNDISPacketHeader.MessageLength = (sizeof(RNDIS_PACKET_MSG_t) + RNDISInterfaceInfo->State.FrameOUT.FrameLength);
			RNDISPacketHeader.DataOffset    = (sizeof(RNDIS_PACKET_MSG_t) - sizeof(RNDIS_Message_Header_t));
			RNDISPacketHeader.DataLength    = RNDISInterfaceInfo->State.FrameOUT.FrameLength;

			Endpoint_Write_Stream_LE(&RNDISPacketHeader, sizeof(RNDIS_PACKET_MSG_t), NO_STREAM_CALLBACK);
			Endpoint_Write_Stream_LE(RNDISInterfaceInfo->State.FrameOUT.FrameData, RNDISPacketHeader.DataLength, NO_STREAM_CALLBACK);
			Endpoint_ClearIN();
			
			RNDISInterfaceInfo->State.FrameOUT.FrameInBuffer = false;
		}
	}
}							

void RNDIS_Device_ProcessRNDISControlMessage(USB_ClassInfo_RNDIS_Device_t* RNDISInterfaceInfo)
{
	/* Note: Only a single buffer is used for both the received message and its response to save SRAM. Because of
	         this, response bytes should be filled in order so that they do not clobber unread data in the buffer. */

	RNDIS_Message_Header_t* MessageHeader = (RNDIS_Message_Header_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;

	switch (MessageHeader->MessageType)
	{
		case REMOTE_NDIS_INITIALIZE_MSG:
			RNDISInterfaceInfo->State.ResponseReady = true;
			
			RNDIS_INITIALIZE_MSG_t*   INITIALIZE_Message  = (RNDIS_INITIALIZE_MSG_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;
			RNDIS_INITIALIZE_CMPLT_t* INITIALIZE_Response = (RNDIS_INITIALIZE_CMPLT_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;
			
			INITIALIZE_Response->MessageType           = REMOTE_NDIS_INITIALIZE_CMPLT;
			INITIALIZE_Response->MessageLength         = sizeof(RNDIS_INITIALIZE_CMPLT_t);
			INITIALIZE_Response->RequestId             = INITIALIZE_Message->RequestId;
			INITIALIZE_Response->Status                = REMOTE_NDIS_STATUS_SUCCESS;
			
			INITIALIZE_Response->MajorVersion          = REMOTE_NDIS_VERSION_MAJOR;
			INITIALIZE_Response->MinorVersion          = REMOTE_NDIS_VERSION_MINOR;			
			INITIALIZE_Response->DeviceFlags           = REMOTE_NDIS_DF_CONNECTIONLESS;
			INITIALIZE_Response->Medium                = REMOTE_NDIS_MEDIUM_802_3;
			INITIALIZE_Response->MaxPacketsPerTransfer = 1;
			INITIALIZE_Response->MaxTransferSize       = (sizeof(RNDIS_PACKET_MSG_t) + ETHERNET_FRAME_SIZE_MAX);
			INITIALIZE_Response->PacketAlignmentFactor = 0;
			INITIALIZE_Response->AFListOffset          = 0;
			INITIALIZE_Response->AFListSize            = 0;
			
			RNDISInterfaceInfo->State.CurrRNDISState = RNDIS_Initialized;
		
			break;
		case REMOTE_NDIS_HALT_MSG:
			RNDISInterfaceInfo->State.ResponseReady = false;
			MessageHeader->MessageLength = 0;

			RNDISInterfaceInfo->State.CurrRNDISState = RNDIS_Uninitialized;

			break;
		case REMOTE_NDIS_QUERY_MSG:
			RNDISInterfaceInfo->State.ResponseReady = true;
						
			RNDIS_QUERY_MSG_t*   QUERY_Message  = (RNDIS_QUERY_MSG_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;
			RNDIS_QUERY_CMPLT_t* QUERY_Response = (RNDIS_QUERY_CMPLT_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;
			uint32_t             Query_Oid      = QUERY_Message->Oid;
						
			void*     QueryData                 = &RNDISInterfaceInfo->State.RNDISMessageBuffer[sizeof(RNDIS_Message_Header_t) +
			                                                                              QUERY_Message->InformationBufferOffset];
			void*     ResponseData              = &RNDISInterfaceInfo->State.RNDISMessageBuffer[sizeof(RNDIS_QUERY_CMPLT_t)];		
			uint16_t  ResponseSize;

			QUERY_Response->MessageType         = REMOTE_NDIS_QUERY_CMPLT;
			QUERY_Response->MessageLength       = sizeof(RNDIS_QUERY_CMPLT_t);
						
			if (RNDIS_Device_ProcessNDISQuery(RNDISInterfaceInfo, Query_Oid, QueryData, QUERY_Message->InformationBufferLength,
			                                  ResponseData, &ResponseSize))
			{
				QUERY_Response->Status                  = REMOTE_NDIS_STATUS_SUCCESS;
				QUERY_Response->MessageLength          += ResponseSize;
							
				QUERY_Response->InformationBufferLength = ResponseSize;
				QUERY_Response->InformationBufferOffset = (sizeof(RNDIS_QUERY_CMPLT_t) - sizeof(RNDIS_Message_Header_t));
			}
			else
			{				
				QUERY_Response->Status                  = REMOTE_NDIS_STATUS_NOT_SUPPORTED;

				QUERY_Response->InformationBufferLength = 0;
				QUERY_Response->InformationBufferOffset = 0;
			}
			
			break;
		case REMOTE_NDIS_SET_MSG:
			RNDISInterfaceInfo->State.ResponseReady = true;
			
			RNDIS_SET_MSG_t*   SET_Message  = (RNDIS_SET_MSG_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;
			RNDIS_SET_CMPLT_t* SET_Response = (RNDIS_SET_CMPLT_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;
			uint32_t           SET_Oid      = SET_Message->Oid;

			SET_Response->MessageType       = REMOTE_NDIS_SET_CMPLT;
			SET_Response->MessageLength     = sizeof(RNDIS_SET_CMPLT_t);
			SET_Response->RequestId         = SET_Message->RequestId;

			void* SetData                   = &RNDISInterfaceInfo->State.RNDISMessageBuffer[sizeof(RNDIS_Message_Header_t) +
			                                                                          SET_Message->InformationBufferOffset];
						
			if (RNDIS_Device_ProcessNDISSet(RNDISInterfaceInfo, SET_Oid, SetData, SET_Message->InformationBufferLength))
			  SET_Response->Status        = REMOTE_NDIS_STATUS_SUCCESS;
			else
			  SET_Response->Status        = REMOTE_NDIS_STATUS_NOT_SUPPORTED;

			break;
		case REMOTE_NDIS_RESET_MSG:
			RNDISInterfaceInfo->State.ResponseReady = true;
			
			RNDIS_RESET_CMPLT_t* RESET_Response = (RNDIS_RESET_CMPLT_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;

			RESET_Response->MessageType         = REMOTE_NDIS_RESET_CMPLT;
			RESET_Response->MessageLength       = sizeof(RNDIS_RESET_CMPLT_t);
			RESET_Response->Status              = REMOTE_NDIS_STATUS_SUCCESS;
			RESET_Response->AddressingReset     = 0;

			break;
		case REMOTE_NDIS_KEEPALIVE_MSG:
			RNDISInterfaceInfo->State.ResponseReady = true;
			
			RNDIS_KEEPALIVE_MSG_t*   KEEPALIVE_Message  = (RNDIS_KEEPALIVE_MSG_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;
			RNDIS_KEEPALIVE_CMPLT_t* KEEPALIVE_Response = (RNDIS_KEEPALIVE_CMPLT_t*)&RNDISInterfaceInfo->State.RNDISMessageBuffer;

			KEEPALIVE_Response->MessageType     = REMOTE_NDIS_KEEPALIVE_CMPLT;
			KEEPALIVE_Response->MessageLength   = sizeof(RNDIS_KEEPALIVE_CMPLT_t);
			KEEPALIVE_Response->RequestId       = KEEPALIVE_Message->RequestId;
			KEEPALIVE_Response->Status          = REMOTE_NDIS_STATUS_SUCCESS;
			
			break;
	}
}

static bool RNDIS_Device_ProcessNDISQuery(USB_ClassInfo_RNDIS_Device_t* RNDISInterfaceInfo,
                                          uint32_t OId, void* QueryData, uint16_t QuerySize,
                                          void* ResponseData, uint16_t* ResponseSize)
{
	switch (OId)
	{
		case OID_GEN_SUPPORTED_LIST:
			*ResponseSize = sizeof(AdapterSupportedOIDList);
			
			memcpy_P(ResponseData, AdapterSupportedOIDList, sizeof(AdapterSupportedOIDList));
			
			return true;
		case OID_GEN_PHYSICAL_MEDIUM:
			*ResponseSize = sizeof(uint32_t);
			
			/* Indicate that the device is a true ethernet link */
			*((uint32_t*)ResponseData) = 0;
			
			return true;
		case OID_GEN_HARDWARE_STATUS:
			*ResponseSize = sizeof(uint32_t);
			
			*((uint32_t*)ResponseData) = NDIS_HardwareStatus_Ready;
			
			return true;
		case OID_GEN_MEDIA_SUPPORTED:
		case OID_GEN_MEDIA_IN_USE:
			*ResponseSize = sizeof(uint32_t);
			
			*((uint32_t*)ResponseData) = REMOTE_NDIS_MEDIUM_802_3;
			
			return true;
		case OID_GEN_VENDOR_ID:
			*ResponseSize = sizeof(uint32_t);
			
			/* Vendor ID 0x0xFFFFFF is reserved for vendors who have not purchased a NDIS VID */
			*((uint32_t*)ResponseData) = 0x00FFFFFF;
			
			return true;
		case OID_GEN_MAXIMUM_FRAME_SIZE:
		case OID_GEN_TRANSMIT_BLOCK_SIZE:
		case OID_GEN_RECEIVE_BLOCK_SIZE:
			*ResponseSize = sizeof(uint32_t);
			
			*((uint32_t*)ResponseData) = ETHERNET_FRAME_SIZE_MAX;
			
			return true;
		case OID_GEN_VENDOR_DESCRIPTION:
			*ResponseSize = (strlen(RNDISInterfaceInfo->Config.AdapterVendorDescription) + 1);
			
			memcpy(ResponseData, RNDISInterfaceInfo->Config.AdapterVendorDescription, *ResponseSize);
			
			return true;
		case OID_GEN_MEDIA_CONNECT_STATUS:
			*ResponseSize = sizeof(uint32_t);
			
			*((uint32_t*)ResponseData) = REMOTE_NDIS_MEDIA_STATE_CONNECTED;
			
			return true;
		case OID_GEN_LINK_SPEED:
			*ResponseSize = sizeof(uint32_t);
			
			/* Indicate 10Mb/s link speed */
			*((uint32_t*)ResponseData) = 100000;

			return true;
		case OID_802_3_PERMANENT_ADDRESS:
		case OID_802_3_CURRENT_ADDRESS:
			*ResponseSize = sizeof(MAC_Address_t);
			
			memcpy(ResponseData, &RNDISInterfaceInfo->Config.AdapterMACAddress, sizeof(MAC_Address_t));

			return true;
		case OID_802_3_MAXIMUM_LIST_SIZE:
			*ResponseSize = sizeof(uint32_t);
			
			/* Indicate only one multicast address supported */
			*((uint32_t*)ResponseData) = 1;
		
			return true;
		case OID_GEN_CURRENT_PACKET_FILTER:
			*ResponseSize = sizeof(uint32_t);
			
			*((uint32_t*)ResponseData) = RNDISInterfaceInfo->State.CurrPacketFilter;
		
			return true;			
		case OID_GEN_XMIT_OK:
		case OID_GEN_RCV_OK:
		case OID_GEN_XMIT_ERROR:
		case OID_GEN_RCV_ERROR:
		case OID_GEN_RCV_NO_BUFFER:
		case OID_802_3_RCV_ERROR_ALIGNMENT:
		case OID_802_3_XMIT_ONE_COLLISION:
		case OID_802_3_XMIT_MORE_COLLISIONS:
			*ResponseSize = sizeof(uint32_t);
			
			/* Unused statistic OIDs - always return 0 for each */
			*((uint32_t*)ResponseData) = 0;
		
			return true;
		case OID_GEN_MAXIMUM_TOTAL_SIZE:
			*ResponseSize = sizeof(uint32_t);
			
			/* Indicate maximum overall buffer (Ethernet frame and RNDIS header) the adapter can handle */
			*((uint32_t*)ResponseData) = (RNDIS_MESSAGE_BUFFER_SIZE + ETHERNET_FRAME_SIZE_MAX);
		
			return true;
		default:
			return false;
	}
}

static bool RNDIS_Device_ProcessNDISSet(USB_ClassInfo_RNDIS_Device_t* RNDISInterfaceInfo, uint32_t OId, void* SetData,
                                        uint16_t SetSize)
{
	switch (OId)
	{
		case OID_GEN_CURRENT_PACKET_FILTER:
			RNDISInterfaceInfo->State.CurrPacketFilter = *((uint32_t*)SetData);
			RNDISInterfaceInfo->State.CurrRNDISState = ((RNDISInterfaceInfo->State.CurrPacketFilter) ?
			                                      RNDIS_Data_Initialized : RNDIS_Data_Initialized);
			
			return true;
		case OID_802_3_MULTICAST_LIST:
			/* Do nothing - throw away the value from the host as it is unused */
		
			return true;
		default:
			return false;
	}
}

#endif
