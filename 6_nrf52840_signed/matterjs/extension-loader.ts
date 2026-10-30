/**
 * matterjs-server definition of the ExtensionLoaderCluster, for a server you run
 * yourself: copy to packages/custom-clusters/src/clusters/ and export it from
 * index.ts there. See src/llext/ExtensionLoaderCluster.h for the device's side.
 */

import { attribute, cluster, command, field, map8, mandatory, octstr, uint32, uint8 } from "@matter/main/model";
import { Bytes } from "@matter/main";

class LoadStartRequest {
    @field(0x0, uint32, mandatory)
    totalSize!: number;

    @field(0x1, uint32, mandatory)
    crc32!: number;
}

class LoadChunkRequest {
    @field(0x0, uint32, mandatory)
    offset!: number;

    @field(0x1, octstr, mandatory)
    data!: Bytes;
}

class UnloadRequest {
    @field(0x0, uint8, mandatory)
    slot!: number;
}

@cluster(0xfff1fc01)
export class ExtensionLoaderCluster {
    /** 0 = Idle, 1 = Receiving, 2 = Loaded, 3 = Error. */
    @attribute(0x0000, uint8)
    transferStatus?: number;

    /** Bit N is set while slot N holds a loaded extension. */
    @attribute(0x0002, map8)
    loadedExtensions?: number;

    /** Bit N is set if slot N has a stored extension that wasn't restored at boot. */
    @attribute(0x0003, map8)
    disabledExtensions?: number;

    @command(0x00, LoadStartRequest)
    loadStart(_request: LoadStartRequest): void {}

    @command(0x01, LoadChunkRequest)
    loadChunk(_request: LoadChunkRequest): void {}

    @command(0x02)
    loadDone(): void {}

    @command(0x03, UnloadRequest)
    unload(_request: UnloadRequest): void {}
}
