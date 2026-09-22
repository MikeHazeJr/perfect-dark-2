#ifndef _IN_ASSET_SOURCE_HARNESS_H
#define _IN_ASSET_SOURCE_HARNESS_H

/* Ordinary-client, smoke-only public source -> production consumer checks. */
int assetSourceSceneHarnessRun(void);
int assetSourceAudioHarnessRun(void);
int assetSourceCharacterHarnessRun(void);
int assetSourceNumericHarnessRun(void);

#endif
