class Procdump < Formula
    desc "ProcDump for Mac provides a convenient way for developers to create core dumps of their application based on performance triggers. ProcDump for Mac is part of Sysinternals."
    homepage "https://learn.microsoft.com/en-us/sysinternals/downloads/procdump"
  
    url "file:///Users/marioh/PD/ProcDump-for-Linux/build/procdump.tar.gz"  
    sha256 "8a1bac6857453ad2f63829016ee0c5a44770ddb0e158195d0cb1d756749881a6"  
    version "1.0.0"

    license "MIT"

    def install
      bin.install "procdump"
    end
  end