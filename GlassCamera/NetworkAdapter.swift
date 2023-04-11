//
//  NetworkAdapter.swift
//  GlassCamera
//
//  Created by Doug MacEwen on 4/3/23.
//

import Foundation

private struct PollData: Decodable {
    let id: Int
    let function_id: String
    let data: String
}

private struct ExposureParams: Decodable {
    let aeValue: Int
    let iso: Int
    let exposureDuration: Int
    let focusDistance: Int
}

class NetworkAdapter {
    let cameraService: CameraService
    private var isPollingServer: Bool = true
    private let dispatchQueue = DispatchQueue(label: "NetworkAdapterQueue", qos: .background)
    
    let serverURL = URL(string: "http://192.168.50.101/network_camera/")
    
    init(cameraService: CameraService) {
        self.cameraService = cameraService
    }
    
    func start() {
        self.dispatchQueue.async {
            while(self.isPollingServer) {
                self.pollServer()
                sleep(1)
            }
        }
    }
    
    func end() {
        self.isPollingServer = false
    }
    
    private func pollServer() {
        if let serverURL = serverURL {
            let pollURL = serverURL.appendingPathComponent("poll")
            print("Polling! :: \(pollURL.absoluteString)")
            URLSession.shared.dataTask(with: pollURL) { (data, response, error) in
                if error != nil {
                    print("Got Error! \(String(describing: error))")
                }
                
                guard let data = data else {
                    print("Got Empty Data!")
                    return
                }
                
                let pollData = try! JSONDecoder().decode(PollData.self, from: data)
                print("Got Poll Response :: \(pollData.id) | \(pollData.function_id) | \(pollData.data)")
                self.networkFunctionDispatch(functionName: pollData.function_id)(pollData.data) { result in
                    print("Finished calling Function! Result :: \(result)")
                }
            }.resume()
        } else {
            print("Server URL is not defined!")
        }
    }
    
    private func networkFunctionDispatch(functionName: String) -> ((String, (String) -> Void) -> Void) {
        switch functionName {
        case "no_requests": return noRequests
        case "preview_disable_auto_exposure": return preview_disable_auto_exposure_handler
        case "preview_update_auto_exposure": return preview_update_auto_exposure_handler
        case "preview_get_exposure_params": return preview_get_exposure_params_handler
        case "preview_get_camera_capabilities": return preview_get_camera_capabilities_handler
        case "preview_update_roi": return preview_update_roi_handler
        case "preview_end_session": return preview_end_session_handler
        case "preview_start_session": return preview_start_session_handler
        case "preview_next_frame": return preview_next_frame_handler
        case "capture_update_exposure_params": return capture_update_exposure_params_handler
        case "capture_get_sensor_crop": return capture_get_sensor_crop_handler
        case "capture_get_sensor_resolution": return capture_get_sensor_resolution_handler
        case "capture_update_roi": return capture_update_roi_handler
        case "capture_end_session": return capture_end_session_handler
        case "capture_start_hibernate": return capture_start_hibernate_handler
        case "capture_end_hibernate": return capture_end_hibernate_handler
        case "capture_start_session": return capture_start_session_handler
        case "capture_start_capture": return capture_start_capture_handle
        default: return noFunctionFound
        }
        
    }
    
    private func noRequests(data: String, cb: (String) -> Void) { cb("No Op") }
    

    private func preview_disable_auto_exposure_handler(data: String, cb: (String) -> Void) {
        cb("Preview: Disabling auto exposure!")
    }
    
    private func preview_update_auto_exposure_handler(data: String, cb: (String) -> Void) {
        cb("Preview: Update Auto Exposure with exposure params :: \(data)")
        
    }
    
    private func preview_get_exposure_params_handler(data: String, cb: (String) -> Void) {
        cb("Preview: Get Exposure Params Handler")
        
    }
    
    private func preview_get_camera_capabilities_handler(data: String, cb: (String) -> Void) {
        cb("Preview: Get Camera Capabilities!")
    }
    
    private func preview_update_roi_handler(data: String, cb: (String) -> Void) {
        
    }
    
    private func preview_end_session_handler(data: String, cb: (String) -> Void) {
        
    }
    
    private func preview_start_session_handler(data: String, cb: (String) -> Void) {
    }
    
    
    private func preview_next_frame_handler(data: String, cb: (String) -> Void) {
    }
    
    
    private func capture_update_exposure_params_handler(data: String, cb: (String) -> Void) {
        let exposureParams = try! JSONDecoder().decode(ExposureParams.self, from: Data(data.utf8))
        cb("Capture: Update exposure params handler :: \(exposureParams)")
    }
    
    
    private func capture_get_sensor_crop_handler(data: String, cb: (String) -> Void) {
    }
    
    
    private func capture_get_sensor_resolution_handler(data: String, cb: (String) -> Void) {
    }
    
    
    private func capture_update_roi_handler(data: String, cb: (String) -> Void) {
    }
    
    
    private func capture_end_session_handler(data: String, cb: (String) -> Void) {
    }
    
    
    private func capture_start_hibernate_handler(data: String, cb: (String) -> Void) {
    }
    
    
    private func capture_end_hibernate_handler(data: String, cb: (String) -> Void) {
    }
    
    
    private func capture_start_session_handler(data: String, cb: (String) -> Void) {
    }
    
    
    private func capture_start_capture_handle(data: String, cb: (String) -> Void) {
        let exposureParams = try! JSONDecoder().decode(ExposureParams.self, from: Data(data.utf8))
        cb("Capture: Start capture! :: \(exposureParams)")
    }
    
    private func noFunctionFound(data: String, cb: (String) -> Void) {
        cb("ERROR: NO FUNCTION FOUND MATCHING FUNCTION_NAME")
    }
}
