//
//  NetworkAdapter.swift
//  GlassCamera
//
//  Created by Doug MacEwen on 4/3/23.
//

import Foundation
import CoreMedia.CMTime
import AVFoundation
import UIKit

private struct PollData: Decodable {
    let id: Int
    let function_id: String
    let data: String
}

private struct ResponseData: Encodable {
    let id: Int
    let data: Encodable
    
    enum CodingKeys: CodingKey {
        case id
        case data
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(id, forKey: .id)
        try container.encode(data, forKey: .data)
    }
}

private struct ExposureParams: Decodable, Encodable {
    let aeValue: Int
    let iso: Int
    let exposureDuration: Int
    let focusDistance: Float
}

private struct CaptureResults: Encodable {
    let imagePaths: [String]
    let exposureParams: ExposureParams
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
    // private let serverURL = URL(string: "http://192.168.1.2/network_camera/")
    private let serverURL = URL(string: "http://192.168.50.101/network_camera/")
    private let POLLING_INTERVAL = 0.1 //seconds
    private var uploader: AVCapturePhotoCaptureDelegate?
    
    init(cameraService: CameraService) {
        self.cameraService = cameraService
    }
    
    func start() {
        if(!self.isPollingServer) { return }
        self.pollServer()
        DispatchQueue.main.asyncAfter(deadline: .now() + POLLING_INTERVAL, qos: .background) { self.start() }
    }
    
    func end() {
        self.isPollingServer = false
    }
    
    private func pollServer() {
        if let serverURL = serverURL {
            let pollURL = serverURL.appendingPathComponent("poll")
            
            URLSession.shared.dataTask(with: pollURL) { (data, response, error) in
                if error != nil { print("Got Polling Error! \(String(describing: error))") }
                
                guard let data = data else {
                    print("Got Empty Data!")
                    return
                }
                
                let pollData = try! JSONDecoder().decode(PollData.self, from: data)
                self.networkFunctionDispatch(functionName: pollData.function_id)(pollData.data) { resultData in
                    self.respondToServer(id: pollData.id, function_id: pollData.function_id, data: resultData)
                }
            }.resume()
        } else {
            print("Server URL is not defined!")
        }
    }
    
    private func respondToServer(id: Int, function_id: String, data: Encodable) {
        if let serverURL = serverURL {
            let responseURL = serverURL.appendingPathComponent("response")
            
            let response = ResponseData(id: id, data: data)
            let responseData = try! JSONEncoder().encode(response)
            
            var responseURLRequest = URLRequest(url: responseURL)
            responseURLRequest.httpMethod = "POST"
            
            URLSession.shared.uploadTask(with: responseURLRequest, from: responseData) {data, response, error in
                if error != nil { print("Got Upload Error! \(String(describing: error))") }
            }.resume()
            
        } else {
            print("Server URL is not defined!")
        }
    }
    
    private func uploadImageToServer(imageData: Data) {
        if let serverURL = serverURL {
            let responseURL = serverURL.appendingPathComponent("image_data_response")
            var responseURLRequest = URLRequest(url: responseURL)
            responseURLRequest.httpMethod = "POST"
            
            print("Beginning Upload!")
            
            URLSession.shared.uploadTask(with: responseURLRequest, from: imageData) {data, response, error in
                if error != nil { print("Got Upload Error! \(String(describing: error))") }
                print("DONE UPLOAD!")
            }.resume()
        } else {
            print("Server URL is not defined!")
        }
    }
    
    private func networkFunctionDispatch(functionName: String) -> ((String, @escaping (Encodable) -> Void) -> Void) {
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
    
    private func noRequests(data: String, cb: (Encodable) -> Void) { /*print("No Pending Commands")*/ }
    private func noFunctionFound(data: String, cb: (Encodable) -> Void) { cb("ERROR: NO FUNCTION FOUND MATCHING FUNCTION_NAME") }
    private func notImplemented(data: String, cb: (Encodable) -> Void) { cb("ERROR: FUNCTION NOT IMPLEMENTED") }

    private func preview_disable_auto_exposure_handler(data: String, cb: (Encodable) -> Void) {
        cb("Preview: Disabling auto exposure!")
    }
    
    private func preview_update_auto_exposure_handler(data: String, cb: @escaping (Encodable) -> Void) {
        let exposureParams = try! JSONDecoder().decode(ExposureParams.self, from: Data(data.utf8))
        print("Preview: Update exposure params handler :: \(exposureParams)")
        
        let minIso = Int(cameraService.videoDeviceInput.device.activeFormat.minISO)
        let maxIso = Int(cameraService.videoDeviceInput.device.activeFormat.maxISO)
        let minExposureDuration = Int(cameraService.videoDeviceInput.device.activeFormat.minExposureDuration.toMicroSeconds())
        let maxExposureDuration = Int(cameraService.videoDeviceInput.device.activeFormat.maxExposureDuration.toMicroSeconds())
        
        if (exposureParams.exposureDuration < minExposureDuration)
            || (exposureParams.exposureDuration > maxExposureDuration)
            || (exposureParams.iso < minIso)
            || (exposureParams.iso > maxIso){
                let message = "Exposure Params(\(exposureParams.exposureDuration), \(exposureParams.iso)) not within device range (\(minExposureDuration), \(maxExposureDuration)), (\(minIso), \(maxIso)). Skipping."
                print(message)
                cb(message)
                return
        }
        
        self.cameraService.setExposureParams(exposureDuration: exposureParams.exposureDuration, iso: exposureParams.iso, cb: cb)
    }
    
    private func preview_get_exposure_params_handler(data: String, cb: (Encodable) -> Void) {
        let exposureParams = ExposureParams(aeValue: 50,
                                            iso: Int(cameraService.videoDeviceInput.device.iso),
                                            exposureDuration: cameraService.videoDeviceInput.device.exposureDuration.toMicroSeconds(),
                                            focusDistance: cameraService.videoDeviceInput.device.lensPosition)
        
        cb(exposureParams)
    }
    
    private func preview_get_camera_capabilities_handler(data: String, cb: (Encodable) -> Void) {
        let minIso = cameraService.videoDeviceInput.device.activeFormat.minISO
        let maxIso = cameraService.videoDeviceInput.device.activeFormat.maxISO
        
        let minExposureDuration = cameraService.videoDeviceInput.device.activeFormat.minExposureDuration
        let maxExposureDuration = cameraService.videoDeviceInput.device.activeFormat.maxExposureDuration
        
        let captureParamRange = ServerCaptureParamsRange(minIso: Int(minIso),
                                                         maxIso: Int(maxIso),
                                                         minExposureDuration: minExposureDuration.toMicroSeconds(),
                                                         maxExposureDuration: maxExposureDuration.toMicroSeconds())
        
        cb(captureParamRange)
    }
    
    private func capture_update_exposure_params_handler(data: String, cb: @escaping (Encodable) -> Void) {
        let exposureParams = try! JSONDecoder().decode(ExposureParams.self, from: Data(data.utf8))
        print("Capture: Update exposure params handler :: \(exposureParams)")
        
        let minIso = Int(cameraService.videoDeviceInput.device.activeFormat.minISO)
        let maxIso = Int(cameraService.videoDeviceInput.device.activeFormat.maxISO)
        let minExposureDuration = Int(cameraService.videoDeviceInput.device.activeFormat.minExposureDuration.toMicroSeconds())
        let maxExposureDuration = Int(cameraService.videoDeviceInput.device.activeFormat.maxExposureDuration.toMicroSeconds())
        
        if (exposureParams.exposureDuration < minExposureDuration)
            || (exposureParams.exposureDuration > maxExposureDuration)
            || (exposureParams.iso < minIso)
            || (exposureParams.iso > maxIso){
                let message = "Exposure Params(\(exposureParams.exposureDuration), \(exposureParams.iso)) not within device range (\(minExposureDuration), \(maxExposureDuration)), (\(minIso), \(maxIso)). Skipping."
                print(message)
                cb(message)
                return
        }
        
        self.cameraService.setExposureParams(exposureDuration: exposureParams.exposureDuration, iso: exposureParams.iso, cb: cb)
    }
    
    private func capture_start_capture_handle(data: String, cb: (Encodable) -> Void) {
        self.uploader = self.cameraService.captureRawPhoto() { rawData in
            print("Got the raw data in callback! \(rawData.count)")
            
            self.uploadImageToServer(imageData: rawData)
        }
        
        let exposureParams = ExposureParams(aeValue: 50,
                                            iso: Int(cameraService.videoDeviceInput.device.iso),
                                            exposureDuration: cameraService.videoDeviceInput.device.exposureDuration.toMicroSeconds(),
                                            focusDistance: cameraService.videoDeviceInput.device.lensPosition)
        
        let captureResult = CaptureResults(imagePaths: [], exposureParams: exposureParams)
        cb([captureResult])
    }
}

class RawUploader: NSObject, AVCapturePhotoCaptureDelegate {
    let rawDataCB: (Data) -> Void
    
    init(rawDataCB: @escaping (Data) -> Void) {
        self.rawDataCB = rawDataCB
    }

    func photoOutput(_ output: AVCapturePhotoOutput, didFinishProcessingPhoto photo: AVCapturePhoto, error: Error?) {
        print("Got to didFinishProcessingPhoto!")
        if let error = error { fatalError("Error capturing photo: \(error)") }
        
        if photo.isRawPhoto {
            if let captureData = photo.fileDataRepresentation() {
                self.rawDataCB(captureData)
            } else {
                fatalError("Could not get file data representation!")
            }
        } else { print("Got Non Raw Photo!") }
    }
}

extension CameraService {
    func setExposureParams(exposureDuration: Int, iso: Int, cb: @escaping (Encodable) -> Void) {
        print("Setting exposure params to :: \(exposureDuration), \(iso)")
        let exposureDuration = CMTime(value: Int64(exposureDuration), timescale: 1_000_000)
        let iso = Float(iso)
        
        sessionQueue.async {
            guard let device = self.videoDeviceInput?.device else { return }
            do {
                try device.lockForConfiguration()
                
                device.focusMode = .locked
                device.exposureMode = .custom
                device.setExposureModeCustom(duration: exposureDuration, iso: iso) { _ in
                    cb("Settings Updated")
                }
                

                device.unlockForConfiguration()
            } catch {
                print("Could not lock device for configuration: \(error)")
            }
        }
    }
    
    public func captureRawPhoto(dataCB: @escaping (Data) -> Void) -> AVCapturePhotoCaptureDelegate? {
        if self.setupResult != .configurationFailed {
            // Take the device orientation into account
            let videoPreviewLayerOrientation = AVCaptureVideoOrientation(deviceOrientation: UIDevice.current.orientation)

            self.isCameraButtonDisabled = true

            if let photoOutputConnection = self.photoOutput.connection(with: .video) {
                if let videoPreviewLayerOrientation = videoPreviewLayerOrientation {
                    photoOutputConnection.videoOrientation = videoPreviewLayerOrientation
                } else {
                    photoOutputConnection.videoOrientation = .portrait
                }
            }

            let query = self.photoOutput.isAppleProRAWEnabled ?
                { !AVCapturePhotoOutput.isAppleProRAWPixelFormat($0) } :
                { AVCapturePhotoOutput.isBayerRAWPixelFormat($0) }

            var photoSettings: AVCapturePhotoSettings

            // Retrieve the RAW format, favoring the Apple ProRAW format when it's in an enabled state.
            guard let rawFormat = self.photoOutput.availableRawPhotoPixelFormatTypes.first(where: query) else {
                fatalError("Raw not available")
            }
            
            // Capture a RAW format photo, along with a processed format photo.
            photoSettings = AVCapturePhotoSettings(rawPixelFormatType: rawFormat)

            if self.videoDeviceInput.device.isFlashAvailable { photoSettings.flashMode = .off }

            let rawUploader = RawUploader(rawDataCB: dataCB)

            self.photoOutput.capturePhoto(with: photoSettings, delegate: rawUploader)
            return rawUploader
        }
        return nil
    }
}
