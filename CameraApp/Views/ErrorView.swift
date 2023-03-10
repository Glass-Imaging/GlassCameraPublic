// Copyright (c) 2021-2022 Glass Imaging Inc.
// Author: Fabio Riccardi <fabio@glass-imaging.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
