import SwiftUI

struct ControlView: View {
    var body: some View {
        VStack{
            Spacer()
            HStack {
                Button {
                    PhotoManager.take()
                } label: {
                    Image(systemName: "camera.fill")
                }.font(.largeTitle)
                    .buttonStyle(.borderless)
                    .controlSize(.large)
                    .tint(.accentColor)
                    .padding(10)
            }
        }
    }
}

struct ControlView_Previews: PreviewProvider {
    static var previews: some View {
        ControlView()
    }
}
