// descriptor-reader.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <windows.h> // This is required to include before setupapi.h
#include <setupapi.h>
#include <Cfgmgr32.h>
#include <iostream>
#include <vector>
#include <string>
#include <initguid.h>
#include <Usbiodef.h>
#include <Usb100.h>
#include <Usbioctl.h>

/**
 * @brief Device structure to represent a USB device
 * 
 */
struct Device
{
	/**
	 * @brief Device path
	 * 
	 */
	std::string device_path;
	/**
	 * @brief Device descriptor
	 * 
	 */
	USB_DEVICE_DESCRIPTOR usb_device_descriptor;
	/**
	 * @brief Manufacture name
	 * 
	 */
	std::string manufacture;
	/**
	 * @brief Serial number
	 * 
	 */
	std::string serial;
	/**
	 * @brief Product name
	 * 
	 */
	std::string product;
};

/**
 * @brief Get the Hub Path object
 * 
 * @param device_instance Device instance of the hub
 * @return std::wstring Device path of the hub associated with provided device instance
 */
std::wstring GetHubPath(DEVINST device_instance)
{
	std::wstring hub_path;
	HDEVINFO device_information = SetupDiGetClassDevs((LPGUID)&GUID_DEVINTERFACE_USB_HUB, NULL, NULL, (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
	DWORD member_index = 0;
	DWORD last_error = NO_ERROR;
	SP_DEVINFO_DATA device_information_data;;
	SP_DEVICE_INTERFACE_DATA  device_interface_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA  device_interface_detail_data;

	if (INVALID_HANDLE_VALUE != device_information)
	{
		while (ERROR_NO_MORE_ITEMS != GetLastError())
		{
			device_information_data.cbSize = sizeof(SP_DEVINFO_DATA);

			if (SetupDiEnumDeviceInfo(device_information, member_index, &device_information_data))
			{
				if (device_information_data.DevInst == device_instance)
				{
					device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

					if (SetupDiEnumDeviceInterfaces(device_information, NULL, (LPGUID)&GUID_DEVINTERFACE_USB_HUB, member_index, &device_interface_data))
					{
						DWORD required_size = 0;
						SetupDiGetDeviceInterfaceDetail(device_information, &device_interface_data, NULL, 0, &required_size, NULL);
						device_interface_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(required_size);
						device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
						if (SetupDiGetDeviceInterfaceDetail(device_information, &device_interface_data, device_interface_detail_data, required_size, NULL, NULL))
						{
							hub_path = device_interface_detail_data->DevicePath;
						}

						if (NULL != device_interface_detail_data)
						{
							free(device_interface_detail_data);
						}
					}
				}
			}
			member_index++;
		}
	}
	return hub_path;
}

/**
 * @brief Get the Port From Location information text
 * 
 * @param location_information Location information
 * @return long Port number
 */
long GetPortFromLocation(const std::wstring& location_information)
{
	size_t port_starting_index = location_information.find_first_of('#');
	size_t port_ending_index = location_information.find_first_of('.');

	std::wstring port = location_information.substr(port_starting_index + 1, (port_ending_index - port_starting_index) - 1);
	return std::stol(port.c_str());
}

/**
 * @brief Get the String Descriptor object
 * 
 * @param hub_path Hub path
 * @param port Device port number
 * @param index String descriptor index
 * @return std::string Descriptor value
 */
std::string GetStringDescriptor(std::wstring hub_path, ULONG port, USHORT index)
{
	HANDLE hub_handle = CreateFile(hub_path.c_str(),
		GENERIC_WRITE | GENERIC_READ,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		NULL,
		NULL);
	if (INVALID_HANDLE_VALUE != hub_handle)
	{
		ULONG   buffer_size = 0;
		ULONG   returned_buffer_size = 0;

		UCHAR   string_descriptor_buffer[sizeof(USB_DESCRIPTOR_REQUEST) + MAXIMUM_USB_STRING_LENGTH];

		PUSB_DESCRIPTOR_REQUEST usb_descriptor_request = NULL;

		buffer_size = sizeof(string_descriptor_buffer);

		usb_descriptor_request = (PUSB_DESCRIPTOR_REQUEST)string_descriptor_buffer;
		PUSB_STRING_DESCRIPTOR  usb_string_descriptor = (PUSB_STRING_DESCRIPTOR)(usb_descriptor_request + 1);
		
		memset(usb_descriptor_request, 0, buffer_size);

		usb_descriptor_request->ConnectionIndex = port;

		usb_descriptor_request->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8) | index;

		usb_descriptor_request->SetupPacket.wIndex = 1033; // en-US

		usb_descriptor_request->SetupPacket.wLength = (USHORT)(buffer_size - sizeof(USB_DESCRIPTOR_REQUEST));

		
		if (DeviceIoControl(hub_handle,
			IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
			usb_descriptor_request,
			buffer_size,
			usb_descriptor_request,
			buffer_size,
			&returned_buffer_size,
			NULL))
		{
			std::wstring wstring(usb_string_descriptor->bString);
			std::string usb_string_descriptor(wstring.begin(), wstring.end());
			return usb_string_descriptor;
		}
		CloseHandle(hub_handle);
	}
	return "";
}

/**
 * @brief Get the Device Descriptor 
 * 
 * @param hub_path Device path of the hub
 * @param port Port number of the device
 * @return USB_DEVICE_DESCRIPTOR Device descriptor
 */
USB_DEVICE_DESCRIPTOR GetDeviceDescriptor(std::wstring hub_path, ULONG port)
{
	USB_DEVICE_DESCRIPTOR usb_device_descriptor = {};
	HANDLE hub_handle = CreateFile(hub_path.c_str(),
		GENERIC_WRITE | GENERIC_READ,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		NULL,
		NULL);
	if (INVALID_HANDLE_VALUE != hub_handle)
	{
		// There can be a maximum of 30 endpoints per device configuration. 
		// So allocate space to hold info for 30 pipes.

		ULONG buffer_size = sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + (sizeof(USB_PIPE_INFO) * 30);

		PUSB_NODE_CONNECTION_INFORMATION_EX usb_node_connection_information_ex = (PUSB_NODE_CONNECTION_INFORMATION_EX)malloc(buffer_size);
		usb_node_connection_information_ex->ConnectionIndex = port;

		if (DeviceIoControl(hub_handle,
			IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
			usb_node_connection_information_ex,
			buffer_size,
			usb_node_connection_information_ex,
			buffer_size,
			&buffer_size,
			NULL))
		{
			usb_device_descriptor = usb_node_connection_information_ex->DeviceDescriptor;
		}
		if (NULL != usb_node_connection_information_ex)
		{
			free(usb_node_connection_information_ex);
		}
		CloseHandle(hub_handle);
	}
	return usb_device_descriptor;
}

/**
 * @brief Get the Devices connected to host
 * 
 * @param guid GUID of the devices interested
 * @return std::vector<Device*> Vector of devices
 */
std::vector<Device*> GetDevices(GUID* guid)
{
	std::vector<Device*> devices;
	HDEVINFO device_information = SetupDiGetClassDevs(guid, NULL, NULL, (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
	DWORD member_index = 0;
	DWORD last_error = NO_ERROR;
	SP_DEVINFO_DATA device_information_data;;
	SP_DEVICE_INTERFACE_DATA  device_interface_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA  device_interface_detail_data;

	if (INVALID_HANDLE_VALUE != device_information)
	{
		while (ERROR_NO_MORE_ITEMS != GetLastError())
		{
			Device *device = new Device();
			device_information_data.cbSize = sizeof(SP_DEVINFO_DATA);

			if (SetupDiEnumDeviceInfo(device_information, member_index, &device_information_data))
			{
				device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

				if (SetupDiEnumDeviceInterfaces(device_information, NULL, guid, member_index, &device_interface_data))
				{
					DWORD required_size = 0;
					SetupDiGetDeviceInterfaceDetail(device_information, &device_interface_data, NULL, 0, &required_size, NULL);
					device_interface_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(required_size);
					device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
					if (SetupDiGetDeviceInterfaceDetail(device_information, &device_interface_data, device_interface_detail_data, required_size, NULL, NULL))
					{
						std::wstring wstring(device_interface_detail_data->DevicePath);
						std::string device_path(wstring.begin(), wstring.end());
						device->device_path = device_path;

						WCHAR location_information[MAX_PATH];
						if (SetupDiGetDeviceRegistryProperty(device_information,
							&device_information_data,
							SPDRP_LOCATION_INFORMATION,
							NULL,
							(BYTE*)location_information,
							MAX_PATH,
							NULL))
						{
							DEVINST parent_device_instance = 0;
							CONFIGRET config_return = CM_Get_Parent(&parent_device_instance, device_information_data.DevInst, 0);
							if (CR_SUCCESS == config_return)
							{
								USB_DEVICE_DESCRIPTOR usb_device_descriptor = GetDeviceDescriptor(GetHubPath(parent_device_instance),GetPortFromLocation(location_information));
								device->manufacture = GetStringDescriptor(GetHubPath(parent_device_instance), GetPortFromLocation(location_information), usb_device_descriptor.iManufacturer);
								device->serial = GetStringDescriptor(GetHubPath(parent_device_instance), GetPortFromLocation(location_information), usb_device_descriptor.iSerialNumber);
								device->product = GetStringDescriptor(GetHubPath(parent_device_instance), GetPortFromLocation(location_information), usb_device_descriptor.iProduct);
								device->usb_device_descriptor = usb_device_descriptor;
								devices.push_back(device);
							}
						}
					}
					else
					{
						last_error = GetLastError();
					}
					if (NULL != device_interface_detail_data)
					{
						free(device_interface_detail_data);
					}
				}
				else
				{
					last_error = GetLastError();
				}
			}
			else
			{
				last_error = GetLastError();
			}
			member_index++;
		}
	}
	else
	{
		last_error = GetLastError();
	}
	return devices;
}



int main()
{
	std::vector<Device*> devices = GetDevices((LPGUID)&GUID_DEVINTERFACE_USB_DEVICE);
	for (auto device : devices)
	{
		std::cout << "---------------------------------------------------- \n";
		std::cout << "Device Path = "<< device->device_path << "\n";
		std::cout << "Vendor Id = " << device->usb_device_descriptor.idVendor << "\n";
		std::cout << "Product Id = " << device->usb_device_descriptor.idProduct << "\n";
		std::cout << "Manufacture = " << device->manufacture << "\n";
		std::cout << "Serial = " << device->serial << "\n";
		std::cout << "Product = " << device->product << "\n";
	}
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
