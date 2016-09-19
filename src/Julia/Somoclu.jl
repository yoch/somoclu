module Somoclu

function train(data, codebook, bmus, uMatrix, nDimensions, nVectors, nSomX, nSomY; nEpoch=10, radius0=0, radiusN=1, radiusCooling="linear", scale0=0.1, scaleN=0.01, scaleCooling="linear", kernelType=0, mapType="planar", gridType="square", compact_support=false, gaussian=true)
    if radiusCooling == "linear"
        _radiusCooling = 0
    elseif radiusCooling == "exponential"
        _radiusCooling = 1
    else
        error("Unknown radius cooling")
    end
    if scaleCooling == "linear"
        _scaleCooling = 0
    elseif scaleCooling == "exponential"
        _scaleCooling = 1
    else
        error("Unknown scale cooling")
    end
    if mapType == "planar"
        _mapType = 0
    elseif mapType == "toroid"
        _mapType = 1
    else
        error("Unknown map type")
    end
    if gridType == "square"
        _gridType = 0
    elseif gridType == "hexagonal"
        _gridType = 1
    else
        error("Unknown grid type")
    end
    ccall((:julia_train, "./libsomoclu.so"), Void, (Ptr{Float32}, Cint, Cuint, Cuint, Cuint, Cuint, Cuint, Cuint, Cuint, Cuint, Float32, Float32, Cuint, Cuint, Cuint, Cuint, Bool, Bool, Ptr{Float32}, Cint, Ptr{Cint}, Cint, Ptr{Float32}, Cint), data, length(data), nEpoch, nSomX, nSomY, nDimensions, nVectors, radius0, radiusN, _radiusCooling, scale0, scaleN, _scaleCooling, kernelType, _mapType, _gridType, compact_support, gaussian, codebook, length(codebook), bmus, length(bmus), uMatrix, length(uMatrix))
end

end