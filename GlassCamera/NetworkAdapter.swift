//
//  NetworkAdapter.swift
//  GlassCamera
//
//  Created by Doug MacEwen on 4/3/23.
//

import Foundation
import CoreMedia.CMTime

private struct PollData: Decodable {
    let id: Int
    let function_id: String
    let data: String
}

private struct ResponseData: Encodable {
    let id: Int
    // let data: Data
    let data: String
}

private struct ExposureParams: Decodable, Encodable {
    let aeValue: Int
    let iso: Int
    let exposureDuration: Int
    let focusDistance: Float
}

private struct ServerCaptureParamsRange: Encodable {
    let minIso: Int
    let maxIso: Int
    let minExposureDuration: Int
    let maxExposureDuration: Int
}

extension CMTime {
    func toMicroSeconds() -> Int {
        return Int(self.seconds * pow(10, 6))
    }
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
        func _start() {
            if(!self.isPollingServer) { return }
            self.pollServer()
            self.dispatchQueue.asyncAfter(deadline: .now() + 0.25) { _start() }
        }
        _start()
    }
    
    func end() {
        self.isPollingServer = false
    }
    
    private func pollServer() {
        if let serverURL = serverURL {
            let pollURL = serverURL.appendingPathComponent("poll")
            // print("Polling! :: \(pollURL.absoluteString)")
            URLSession.shared.dataTask(with: pollURL) { (data, response, error) in
                if error != nil {
                    print("Got Polling Error! \(String(describing: error))")
                }
                
                guard let data = data else {
                    print("Got Empty Data!")
                    return
                }
                
                let pollData = try! JSONDecoder().decode(PollData.self, from: data)
                // print("Got Poll Response :: \(pollData.id) | \(pollData.function_id) | \(pollData.data)")
                self.networkFunctionDispatch(functionName: pollData.function_id)(pollData.data) { resultData in
                    // print("Finished calling Function \(pollData.function_id) :: \(String(decoding: resultData, as: UTF8.self))")
                    self.respondToServer(id: pollData.id, function_id: pollData.function_id, data: resultData)
                }
            }.resume()
        } else {
            print("Server URL is not defined!")
        }
    }
    
    private func respondToServer(id: Int, function_id: String, data: Data) {
        if let serverURL = serverURL {
            // let response = ResponseData(id: id, data: "test")
            let response = ResponseData(id: id, data: String(decoding: data, as: UTF8.self))
            // let response = ResponseData(id: id, data: data)
            let responseData = try! JSONEncoder().encode(response)
            // let responseData = "{\"id\": 1, \"data\": \"test\"}".data(using: .utf8)!
            
            let responseURL = serverURL.appendingPathComponent("response")
            var responseURLRequest = URLRequest(url: responseURL)
            responseURLRequest.httpMethod = "POST"
            responseURLRequest.httpBody = responseData
            
            print("Responding! (FID \(function_id)) :: \(responseURL.absoluteString) , \(String(decoding: responseData, as: UTF8.self))")
            URLSession.shared.dataTask(with: responseURLRequest) {data, response, error in
            // URLSession.shared.uploadTask(with: responseURLRequest, from: responseData) {data, response, error in
                if error != nil {
                    print("Got Upload Error! \(String(describing: error))")
                }
                
                print("Upload to server done!")
                
            }.resume()
            
        } else {
            print("Server URL is not defined!")
        }
    }
    
    private func networkFunctionDispatch(functionName: String) -> ((String, (Data) -> Void) -> Void) {
        switch functionName {
        case "no_requests": return noRequests
        case "preview_disable_auto_exposure": return preview_disable_auto_exposure_handler
        case "preview_update_auto_exposure": return preview_update_auto_exposure_handler
        case "preview_get_exposure_params": return preview_get_exposure_params_handler
        case "preview_get_camera_capabilities": return preview_get_camera_capabilities_handler
        case "capture_update_exposure_params": return capture_update_exposure_params_handler
        case "capture_start_capture": return capture_start_capture_handle
        case "preview_update_roi": return notImplemented
        case "preview_end_session": return notImplemented
        case "preview_start_session": return notImplemented
        case "preview_next_frame": return notImplemented
        case "capture_get_sensor_crop": return notImplemented
        case "capture_get_sensor_resolution": return notImplemented
        case "capture_update_roi": return notImplemented
        case "capture_end_session": return notImplemented
        case "capture_start_hibernate": return notImplemented
        case "capture_end_hibernate": return notImplemented
        case "capture_start_session": return notImplemented
        default: return noFunctionFound
        }
        
    }
    
    private func noRequests(data: String, cb: (Data) -> Void) { print("No Pending Commands") }
    

    private func preview_disable_auto_exposure_handler(data: String, cb: (Data) -> Void) {
        cb("Preview: Disabling auto exposure!".data(using: .utf8)!)
    }
    
    private func preview_update_auto_exposure_handler(data: String, cb: (Data) -> Void) {
        cb("Preview: Update Auto Exposure with exposure params :: \(data)".data(using: .utf8)!)
        
    }
    
    private func preview_get_exposure_params_handler(data: String, cb: (Data) -> Void) {
        let exposureParams = ExposureParams(aeValue: 50,
                                            iso: Int(cameraService.videoDeviceInput.device.iso),
                                            exposureDuration: cameraService.videoDeviceInput.device.exposureDuration.toMicroSeconds(),
                                            focusDistance: cameraService.videoDeviceInput.device.lensPosition)
        

        let exposureParamsData = try! JSONEncoder().encode(exposureParams)
        cb(exposureParamsData)
    }
    
    private func preview_get_camera_capabilities_handler(data: String, cb: (Data) -> Void) {
        let minIso = cameraService.videoDeviceInput.device.activeFormat.minISO
        let maxIso = cameraService.videoDeviceInput.device.activeFormat.maxISO
        
        let minExposureDuration = cameraService.videoDeviceInput.device.activeFormat.minExposureDuration
        let maxExposureDuration = cameraService.videoDeviceInput.device.activeFormat.maxExposureDuration
        
        let captureParamRange = ServerCaptureParamsRange(minIso: Int(minIso),
                                                         maxIso: Int(maxIso),
                                                         minExposureDuration: minExposureDuration.toMicroSeconds(),
                                                         maxExposureDuration: maxExposureDuration.toMicroSeconds())
                                                         //minExposureDuration: Int(minExposureDuration.seconds * pow(10, 6)),
                                                         //maxExposureDuration: Int(maxExposureDuration.seconds * pow(10, 6)))
        
        
        let captureParamRangeString = try! JSONEncoder().encode(captureParamRange)
        cb(captureParamRangeString)
    }
    
    private func capture_update_exposure_params_handler(data: String, cb: (Data) -> Void) {
        let exposureParams = try! JSONDecoder().decode(ExposureParams.self, from: Data(data.utf8))
        cb("Capture: Update exposure params handler :: \(exposureParams)".data(using: .utf8)!)
    }
    
    private func capture_start_capture_handle(data: String, cb: (Data) -> Void) {
        // let exposureParams = try! JSONDecoder().decode(ExposureParams.self, from: Data(data.utf8))
        self.cameraService.capturePhoto()
        cb("Capture: Start capture!".data(using: .utf8)!)// :: \(exposureParams)".data(using: .utf8)!)
    }
    
    private func noFunctionFound(data: String, cb: (Data) -> Void) {
        cb("ERROR: NO FUNCTION FOUND MATCHING FUNCTION_NAME".data(using: .utf8)!)
    }
    
    private func notImplemented(data: String, cb: (Data) -> Void) {
        cb("ERROR: FUNCTION NOT IMPLEMENTED".data(using: .utf8)!)
    }

}

