//
//  ContentView.swift
//  CameraApp
//
//  Created by Fabio Riccardi on 3/9/23.
//

import SwiftUI

struct ContentView: View {
    @StateObject private var model = ContentViewModel()

    var body: some View {
        ZStack {
            FrameView(image: model.frame)
                .edgesIgnoringSafeArea(.all)

            ErrorView(error: model.error)

            ControlView()
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
