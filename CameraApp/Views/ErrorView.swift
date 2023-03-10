import SwiftUI

struct ErrorView: View {
    var error: Error?

    var body: some View {
        self.error != nil ? ErrorMessage(String(describing: self.error)) : nil
    }
}

func ErrorMessage(_ text: String) -> some View {
    return VStack {
        VStack {
            Text("Error Occured").font(.title).padding(.bottom, 5)
            Text(text)
        }.foregroundColor(.white).padding(10).background(Color.red)
        Spacer()
    }
}

struct ErrorView_Previews: PreviewProvider {
    static var previews: some View {
        ErrorView(error: CameraManager.CameraError.cameraUnavailable as Error)
    }
}
