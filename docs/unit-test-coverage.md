# Unit-test coverage

One row per converted C++ function: which gtest case(s) exercise it, and
the result of the last run (`docs/test-results/latest.log`). A function
with no dedicated test still appears, marked accordingly.

- **pass** — has its own passing test case(s)
- **pass (indirect)** — no dedicated test; exercised only through a
  caller's test
- **none** — not covered by any unit test

Last run: **234/234 passing** (2026-09-14). Separately, every non-GUI
phase is MATLAB-parity checked (`matlab_verify/`): Arrays + Util (88/88),
Correction (35/35), Stimulation (28/28), MRI (69/69), Registration (89/89),
Safety (14/14). The serial transport (`libs/serialcom/serial_port`) is OS
I/O with no `.m` counterpart — tested against an in-memory fake; the
`libs/gui` presenters are tested without Qt, the `libs/gui_qt` views +
shell via `apps/beam_app --check`.

## libs/util

| Function | Source | Test | Result |
|---|---|---|---|
| `vectorRange` | `Util/vectorRange.m` | `VectorRange.KnownValue` | pass |
| `distancePointToLine` | `Util/distancePointToLine.m` | `DistancePointToLine.*` (2) | pass |
| `addVectors` | `Util/addVectors.m` | `AddVectors.ElementwiseSum` | pass |
| `angleBetweenTwoVectors` | `Util/angleBetweenTwoVectors.m` | `AngleBetweenTwoVectors.*` (2) | pass |
| `convertDelaysToCycles` | `Util/convertDelaysToCycles.m` | `ConvertDelaysToCycles.ScalesBySingleFrequency` | pass |
| `cam2targetSpace` | `Util/cam2targetSpace.m` | `Cam2TargetSpace.IdentityAndTranslation` | pass |
| `mm2pixel` | `Util/mm2pixel.m` | `Mm2Pixel.ScaleAndOffset` | pass |
| `mm3Dpnp` | `Util/mm3Dpnp.m` | `Mm3Dpnp.RaysHitPlane` | pass |
| `rotm2eulSimple` | `Util/rotm2eul_simple.m` | `Rotm2EulSimple.IdentityAndYaw` | pass |

## libs/array

| Function | Source | Test | Result |
|---|---|---|---|
| `translateAffineMatrix` | `Arrays/defineTranslateAffineMatrix.m` | `Affine.TranslateMovesPoint` | pass |
| `xRotAffineMatrix` / `yRotAffineMatrix` / `zRotAffineMatrix` | `Arrays/{x,y,z}RotAffineMatrix.m` | `Affine.RotationBlocksAreOrthonormal…`, `Affine.ZRotNinety…` | pass |
| `applyAffineToRect` | `Arrays/applyAffineToRect.m` | `Affine.ApplyAffineToRectTranslatesCornersAndCenter` | pass |
| `rectCorner` / `rectCenter` | Beam rect-layout helpers | (used throughout Affine/Geometry tests) | pass (indirect) |
| `normalVectorFrom3Points` | `Arrays/normalVectorFrom3Points.m` | `Geometry.NormalVectorFrom3PointsPicksZeroFacingDirection` | pass |
| `calculateRectNormalVector` | `Arrays/calculateRectNormalVector.m` | `Geometry.CalculateRectNormalVectorIsUnitZForSyntheticElement` | pass |
| `calculateArrayNormalVectors` | `Arrays/calculateArrayNormalVectors.m` | (via `ArrayData.DefineArrayDataUsesBeamsRealConstants`) | pass (indirect) |
| `getElementPositionsFromArrayStruct` | `Arrays/getElementPositionsFromArrayStruct.m` | `Geometry.GetElementPositionsFromArrayStructReturnsRowPerElement` | pass |
| `spatiallySampleElement` | `Arrays/spatiallySampleElement.m` | `Geometry.SpatiallySampleElementCornerMatchesFirstSample` | pass |
| `getReceiveElements` | `Arrays/getReceiveElements.m` | (via `ArrayData.DefineArrayDataUsesBeamsRealConstants`) | pass (indirect) |
| `defineArrayStruct` | `Arrays/defineArrayStruct.m` | (via `ArrayData.DefineArrayDataUsesBeamsRealConstants`) | pass (indirect) |
| `defineArrayData` | `Arrays/defineArrayData.m` | `ArrayData.DefineArrayDataUsesBeamsRealConstants` | pass |
| `defineArrayTxElements` | `Arrays/defineArrayTxElements.m` | `ArrayData.DefineArrayTxElementsModes` | pass |
| `arrayElementsToVSXElements` | `Arrays/arrayElementsToVSXElements.m` | `ArrayData.ArrayElementsToVSXElementsSkipsElements127And128` | pass |
| `getOpposingElements` | `Util/getOpposingElements.m` | `ArrayData.GetOpposingElementsReturnsOtherArraysElementNumbers` | pass |

## libs/correction

| Function | Source | Test | Result |
|---|---|---|---|
| `xcorr` | MATLAB `xcorr` (primitive) | `Xcorr.AutocorrelationOfShortSignal` | pass |
| `corrCoefficient` | MATLAB `corrcoef` | `CorrCoefficient.IdenticalAndOpposite` | pass |
| `nanmean` | MATLAB `nanmean` | `Nanmean.IgnoresNaN` | pass |
| `xcorrS1ToS2` | `Correction/xcorrS1ToS2.m` | `XcorrS1ToS2.RecoversKnownShift` | pass |
| `corrSpeedUp` | `Correction/corrSpeedUp.m` | `CorrSpeedUp.SelfCorrelationPeaksAtLagMinusOne` | pass |
| `computeMaxCorrelationDelays` | `Correction/computeMaxCorrelationDelays.m` | `ComputeMaxCorrelationDelays.AutoPicksStrongestChannelAndSignedDelays` | pass |
| `shiftAndSumWaveforms` | `Correction/shiftAndSumWaveforms.m` | `ShiftAndSumWaveforms.ShiftsRowsAndSumsColumns` | pass |
| `defineTxRxScanParams` | `Correction/defineTxRxScanParams.m` | `DefineTxRxScanParams.LiteralConstants` | pass |
| `getReceiveElementsUnderAngle` | `Correction/getReceiveElementsUnderAngle.m` | `GetReceiveElementsUnderAngle.NormModeSelectsElementsWithinThreshold` | pass |
| `setAdjustedAttValues` | `Correction/setAdjustedAttValues.m` | `SetAdjustedAttValues.*` (2) | pass |
| `calculateAttenuation` | `Correction/calculateAttenuation.m` | `CalculateAttenuation.PeakToPeakRatio` | pass |
| `findPeaks` | MATLAB `findpeaks` | `FindPeaks.StrictLocalMaxima` | pass |
| `findPeakNegativeVoltage` | `BEAMANALYSIS/analysisUtil/findPeakNegativeVoltage.m` | `FindPeakNegativeVoltage.*` (2) | pass |
| `analyticEnvelope` | `abs(hilbert(x))` | `AnalyticEnvelope.ConstantForPureSinusoid` | pass |
| `filterTransmitSignal` | `Correction/filterTransmitSignal.m` | `FilterTransmitSignal.*` (2) | pass |
| `txRxSignalAmplitude` | `BEAMANALYSIS/analysisUtil/getTxRxSignalAmplitude.m` | `TxRxSignalAmplitude.*` (2) | pass |
| `throughTransmitAmplitude` | `Correction/getTransmissionAfterThroughTransmit.m` (computation) | `ThroughTransmitAmplitude.StrongerCouplingGivesHigherAmp` | pass |
| `distance` | `Correction/localizeArrays/distance.m` | `Localize.DistanceIsEuclidean` | pass |
| `updateArrayPositions` | `Correction/localizeArrays/updateArrayPositions.m` | `Localize.UpdateArrayPositionsShiftsCornersAndCenter` | pass |
| `nonlinRelativeDistanceFun` | `Correction/localizeArrays/nonlinRelativeDistanceFun.m` | `Localize.NonlinRelativeDistanceFunResidual` | pass |
| `getLocalizeArraysSystemOfEquations` | `Correction/localizeArrays/getLocalizeArraysSystemOfEquations.m` | `GetLocalizeArraysSystemOfEquations.IdenticalWaveformsGiveZeroDelaySystem` | pass |
| `localizeArrays` | `Correction/localizeArrays/localizeArraysMaster.m` (solver core) | `LocalizeArrays.ReducesObjectiveWithinBounds` | pass |
| `butterBandpass` | MATLAB `butter` (builtin; new infra for `getRecieveWaveformFromSerial.m`) | `ButterBandpass.MatchesMatlabReferenceCoefficients` + MATLAB parity | pass |
| `filterIir` | MATLAB `filter` (builtin) | `FilterIir.*` (2) + MATLAB parity | pass |
| `parseCorrectionWaveformLine` | `Correction/getRecieveWaveformFromSerial.m` (per-line parsing) | `ParseCorrectionWaveformLine.*` (2) | pass |
| `splitAndFilterReceiveWaveform` | `Correction/getRecieveWaveformFromSerial.m` (split/filter/trim core) | `SplitAndFilterReceiveWaveform.*` (2) | pass |

## libs/stimulation

| Function | Source | Test | Result |
|---|---|---|---|
| `calculateMultifrequencySuperpositionDelays` | `Stimulation/calculateMultifrequencySuperpositionDelays.m` | `CalculateMultifrequencySuperpositionDelays.KnownValue` | pass |
| `focusArrayAtPoint` | `Stimulation/focusArrayAtPoint.m` | `FocusArrayAtPoint.SteeringDelaysAndFarthestElement` | pass |
| `getApodFromAtt` | `Stimulation/getApodFromAtt.m` | `GetApodFromAtt.ClampsAndNormalises` | pass |
| `interp1` | MATLAB `interp1` | `Interp1.LinearAndOutOfRange` | pass |
| `pressureToDutyCycleGivenTransmission` | `Stimulation/pressureToDutyCycleGivenTransmission.m` | `PressureToDutyCycleGivenTransmission.CalibrationCurveLookup` | pass |
| `defineStimFreqs` | `Stimulation/defineStimFreqs.m` | `DefineStimFreqs.KnownControlsAndErrors` | pass |
| `getPauseIntervals` | `Stimulation/GeneralSonication/getPauseIntervals.m` | `GetPauseIntervals.SplitsIntoChunks` | pass |
| `defineStimParams` | `Stimulation/defineStimParams.m` | `DefineStimParams.SingleGroupSingleTarget` | pass |
| `getTxAndBurstEvents` | `Stimulation/GeneralSonication/getTxAndBurstEvents.m` | `GetTxAndBurstEvents.*` (2) | pass |
| `computeSonicationEventTimeline` | `Stimulation/GeneralSonication/setBurstEventsFromStimParams.m` (computational core) | `ComputeSonicationEventTimeline.*` (4) | pass |

## libs/mri

| Function | Source | Test | Result |
|---|---|---|---|
| `getRasXformFromHeader` | `MRI/get_ras_xform_fromHdr.m` | `GetRasXformFromHeader.*` (5) | pass |
| `getRasAxisVectors` | `MRI/getRASAxisVectorsFromNifti.m` | `GetRasAxisVectors.*` (2) | pass |
| `getVoxelRasXform` | `nifti_utils/get_voxel_RAS_xform.m` | `GetVoxelRasXform.*` (2) | pass |
| `applyVoxelRasXform3D` | `nifti_utils/vol_apply_xform.m` | `ApplyVoxelRasXform3D.*` (2) | pass |
| `readNiftiHeader` | new (NIfTI-1 reader) | `NiftiFile.RejectsWrongMagic`, `NiftiFile.RejectsCorruptSizeofHdr` | pass |
| `readNiftiVolumeScaled` | new | `NiftiFile.RoundTripsHeaderAndAppliesScaling`, `NiftiFile.ZeroSclSlope…` | pass |
| `writeNiftiVolume` | `MRI/setNiftiFromSys.m` (core) | `WriteNiftiVolume.*` (2) | pass |
| `loadNiftiMriRas` | `MRI/loadMRIRAS.m` (NIfTI branch) | `LoadNiftiMriRas.IdentityQformPassesVoxelsAndAxesThroughUnchanged` | pass |
| `getSliceImage` | `MRI/getSliceImage.m` | `GetSliceImage.*` (3) | pass |
| `reorientedVolumeToVolume3D` | new (not a `.m` port); bridges `loadNiftiMriRas` output to `getSliceImage`'s input | `ReorientedVolumeToVolume3D.ReshapesColumnMajorFlatVoxelsPerKSlice` | pass |
| `applyAffine3D` | `Registration/ImageBasedModel/apply_affine_3d.m` | `ApplyAffine3D.*` (4) | pass |
| `getDicomPixelSpatialReferenceMath` | `MRI/getDicomPixelSpatialReference.m` (coord math) | `GetDicomPixelSpatialReferenceMath.*` (2) | pass |
| `resolveDicomOrientationDefaults` | `MRI/load_dicom_volume_with_coords.m` (coord math) | `ResolveDicomOrientationDefaults.*` (3) | pass |
| `getDicomVolumeCoords` | `MRI/load_dicom_volume_with_coords.m` (coord math) | `GetDicomVolumeCoords.HandComputedLinearSequencesWithIdentityAxes` | pass |
| `buildNiftiHeaderFromDicomSeries` | new (DICOM series geometry) | `BuildNiftiHeaderFromDicomSeries.HandComputedAxialCase` | pass |
| `computeVoxelResolution` | `GUI/updateSysWithMRI.m` (`res` computation) | `ComputeVoxelResolution.*` (2) | pass |
| `computeDisplayWindow` | `GUI/updateSysWithMRI.m` (`window` computation) | `ComputeDisplayWindow.*` (2) | pass |

## libs/infra_dicom

| Function | Source | Test | Result |
|---|---|---|---|
| `readDicomTags` | `MRI/getDicomPixelSpatialReference.m` (I/O) | `DicomTags.*` (2) | pass |
| `readDicomSlice` | `MRI/loadDicomDirRAS.m` (I/O) | `DicomSlice.RoundTripsPixelDataWithRescaleApplied` | pass |
| `assembleDicomSeriesRas` | `MRI/loadDicomDirRAS.m` (replacement) | `AssembleDicomSeriesRas.*` (3) | pass |
| `loadMriRas` | `MRI/loadMRIRAS.m` (dispatcher) | `LoadMriRas.*` (5) | pass |

## libs/registration

| Function | Source | Test | Result |
|---|---|---|---|
| `affineRegistration` | `Registration/affineRegistration.m` | `AffineRegistration.*` (3) | pass |
| `getAffineMatrixFromRegistration` | `Registration/getAffineMatrixFromRegistration.m` | `GetAffineMatrixFromRegistration.PackagesRotationAndScalesTranslation` | pass |
| `getArrayFiducialMarkerNames` | `Registration/getArrayFiducialMarkerNames.m` | `GetArrayFiducialMarkerNames.EightElNames` | pass |
| `setArrayFiducialMarkers` | `Registration/setArrayFiducialMarkers.m` | `SetArrayFiducialMarkers.SixMarkersAtFixedOffsets` | pass |
| `applyAffineMatrixToFiducialMarkers` | `Registration/applyAffineMatrixToFrameData.m` | `ApplyAffineMatrixToFiducialMarkers.TranslatesEveryMarker` | pass |
| `getTransducerFiducialMarkersPositionFromFrame` | `Registration/MRINeuroNav/…/getTransducerFiducialMarkersPositionFromFrame.m` | `GetTransducerFiducialMarkersPositionFromFrame.MovesMarkersBySliderDelta` | pass |
| `getFiducialPositionFromName` | `Registration/MRINeuroNav/…/getFiducialPositionFromName.m` | `GetFiducialPositionFromName.FindsAndThrows` | pass |
| `getTranslationMatrixFromTransducerFiducials` | `Registration/MRINeuroNav/…/getTranslationMatrixFromTransducerFiducials.m` | `GetTranslationMatrixFromTransducerFiducials.IdentityBasisFromCleanAxes` | pass |
| `applyAffineToArrayData` | `Arrays/applyAffineToArrayData.m` | `ApplyAffineToArrayData.IdentityIsNoOpAndTranslationShiftsElements` | pass |
| `orientationAngleFromExif` | `Registration/ImageBasedModel/getImgOrientationAngle.m` | `OrientationAngleFromExif.StandardTags` | pass |
| `perimwalkImage2` | `Registration/ImageBasedModel/Lib/perimwalkImage2.m` | `PerimwalkImage2.OrdersConnectedPixelsFromStart` | pass |
| `perimdistanceImage` | `Registration/ImageBasedModel/Lib/perimdistanceImage.m` | `PerimdistanceImage.ScaledStepNorms` | pass |
| `getDiscreteFromImage` | `Registration/ImageBasedModel/getDiscreteFromImage.m` | `GetDiscreteFromImage.SamplesSevenMarkersAlongAVerticalContour` | pass |
| `organizeTransducerSlots` | `Registration/ImageBasedModel/organizeTransducerSlots.m` | `OrganizeTransducerSlots.SortsSlotsAndPairsWithArrayMarkers` | pass |
| `registerArrayToFiducials` | `Registration/registerArrayToFiducials.m` | `RegisterArrayToFiducials.*` (2) | pass |
| `registerCurrentTransducerPosition` | `Registration/MRINeuroNav/TranslateArrayPosition/registerCurrentTransducerPostion.m` (MRIFiducialsButton branch) | `RegisterCurrentTransducerPosition.*` (3) | pass |

## libs/serialcom

| Function | Source | Test | Result |
|---|---|---|---|
| `setSerialCommandFromStimParams` | `SerialCom/setSerialCommandFromStimParams.m` | `SetSerialCommandFromStimParams.BuildsCommandString` | pass |
| `parseCorrectionReceiveString` | `SerialCom/parseCorrectionReceiveString.m` (parse core) | `ParseCorrectionReceiveString.*` (3) | pass |
| `sendSerialCommand` | `SerialCom/sendSerialCommand.m` | `SendSerialCommand.*` (3) | pass |
| `clearSerial` | `SerialCom/clearSerial.m` | `ClearSerial.DrainsAllQueuedLines` | pass |
| `isSerialHealthy` | `SerialCom/isSerialHealthy.m` | `IsSerialHealthy.*` (2) | pass |
| `listAvailableComPorts` | `SerialCom/listAvailableCOMPorts.m` | `ListAvailableComPorts.DoesNotThrow` | pass (smoke) |
| `SerialPort` (connect/disconnect) | `SerialCom/connectSerial.m`, `disconnectSerial.m` | `SerialPort.OpeningAMissingPortThrows` | pass (partial — needs a device) |

## libs/sham

| Function | Source | Test | Result |
|---|---|---|---|
| `whiteNoise` | `Sham/whiteNoise.m` | `WhiteNoise.*` (2) | pass |
| `setShamAudio` | `Sham/setShamAudio.m` (core) | `SetShamAudio.*` (2) | pass |

## libs/safety

| Function | Source | Test | Result |
|---|---|---|---|
| `isppa` | `GUI/Safety/checkSonicationSafety.m` (formula) | `Intensity.IsppaFormula` | pass |
| `mechanicalIndex` | `GUI/Safety/getMechanicalIndex.m` | `Intensity.MechanicalIndex` | pass |
| `isptaFromParams` | `GUI/Safety/getISPTAFromStimParams.m` (core) | `Intensity.IsptaBurstDurationAndInterval` | pass |
| `maxSteeringRangeDegrees` / `kMaxSonicationAmplitudeMPa` | `GUI/Safety/getMaxSteeringRange.m` (source has zero real callers -- reclassified Excluded 2026-09-12, see `docs/known_gaps_gui.md`; also unused by anything in this port besides this test), `getMaxSonicationAmplitude.m` | `Limits.ConstantsAndSteeringRange` | pass |
| `checkSonicationParameters` | `GUI/Safety/checkSonicationSafety.m` (core) | `CheckSonicationParameters.*` (4) | pass |
| `checkCouplingSafety` | `GUI/Safety/checkCouplingSafety.m` (core) | `CheckCouplingSafety.LowAttenuationWarns` | pass |

## libs/gui

| Function | Source | Test | Result |
|---|---|---|---|
| `checkSonicationSafety` (orchestration) | `GUI/Safety/checkSonicationSafety.m` (preconditions + status) | `CheckSonicationSafety.*` (5) | pass |
| `computeAvgTransmissionBars` | `GUI/CorrectionTab/setAvgTransmissionDataBars.m` | `ComputeAvgTransmissionBars.PassAndColorComeFromDifferentQuantities` | pass |
| `computeRfPlotYLimit` | `GUI/CorrectionTab/updateAttenuationPlots.m` (`ULBound`) | `ComputeRfPlotYLimit.MaxAbsAcrossBothPlusTen` | pass |
| `setCrfDateTime` / `computeCrfAutoSaveFilename` | `GUI/UIFeatures/setCRFDateTime.m`, `setCRFAutoSaveFilename.m` | `CaseReportForm.AutoSaveFilenameMatchesSourceFormat` | pass |
| `buildSessionCaseReportForm` | `GUI/UIFeatures/setSessionCaseReportParameters.m` (unconditional body) | `CaseReportForm.BuildSessionFillsConstantsAndFilename` | pass |
| `getAcpcTransform` | `GUI/RegistrationTab/Targeting/getACPCTransform.m` (source has zero real callers -- reclassified Excluded 2026-09-12, see `docs/known_gaps_gui.md`; correct, tested translation kept in the tree regardless) | `GetAcpcTransform.*` (2) | pass |
| `applyReferenceCoordinateTransform` | `GUI/RegistrationTab/Targeting/applyReferenceCoordinateTransform.m` (source has zero real callers -- reclassified Excluded 2026-09-12, see `docs/known_gaps_gui.md`) | `ApplyReferenceCoordinateTransform.*` (2) | pass |
| `getArrayFiducialNormals` | `GUI/RegistrationTab/AutoReg/getArrayFiducialNormals.m` (source has zero real callers -- reclassified Excluded 2026-09-12, see `docs/known_gaps_gui.md`) | `GetArrayFiducialNormals.*` (2) | pass |
| `getResponseFromTreatmentProtocolData` | `GUI/SonicationTab/getResponseFromTreatmentProtocolData.m` | `GetResponseFromTreatmentProtocolData.*` (2) | pass |
| `getNewProtocolName` | `GUI/SonicationTab/getNewProtocolName.m` | `GetNewProtocolName.*` (2) | pass |
| `colorMapRgb` | `GUI/SonicationTab/setColorMapRGB.m` | `ColorMapRgb.HasEightySevenRowsMatchingSourceBug` | pass |
| `getCurrentShownSonication` | `GUI/SonicationTab/getCurrentShownSonication.m` | `GetCurrentShownSonication.*` (2) | pass |
| `sortSonicationTableOrder` | `GUI/SonicationTab/sortSonicationTable.m` | `SortSonicationTableOrder.ReturnsStableAscendingPermutation` | pass |
| `computePulseWaveformPlot` | `GUI/SonicationTab/updateSonicationPlots.m` (pulse-plot segment) | `ComputePulseWaveformPlot.StepsDownAfterPulseDuration` | pass |
| `getTopTargetsFromTreatmentProtocolTable` | `GUI/SonicationTab/getTopTargetsFromTreatmentProtocolTable.m` | `GetTopTargets.*` (7) | pass |
| `computeTreatmentRowColor` | `GUI/SonicationTab/setTreatmentProtocolTableData.m` (coloring rule) | `ComputeTreatmentRowColor.*` | pass |
| `accFlagForProtocolName` | `GUI/SonicationTab/setTreatmentProtocolTableData.m` (ACCFlag selection) | `AccFlagForProtocolName.*` | pass |
| `imagePositionToVoxelIndex` | `GUI/imagePositionToIJK.m` | `ImagePositionToVoxelIndex.NearestIndexPerAxisIsZeroBased` | pass |
| `rasterizeArrayOntoMriGrid` | `GUI/drawTransducersOnMRI.m` (rasterization loop) | `RasterizeArrayOntoMriGrid.*` (2) | pass |
| `rasterizeFiducialMarkersOntoMriGrid` | new (not a `.m` port); stands in for `GUI/drawROIs.m`'s static-highlight half | `RasterizeFiducialMarkersOntoMriGrid.PaintsACubeCenteredOnTheNearestVoxel` | pass |
| `rasterizeFocusEllipsoidOntoMriGrid` | `GUI/RegistrationTab/AutoReg/setFiducialTemplate.m` (unrotated single-point call site only) | `RasterizeFocusEllipsoidOntoMriGrid.*` (3) | pass |
| `prepareSonication` | `Stimulation/GeneralSonication/generalSonicateMaster.m` (decision logic) | `PrepareSonication.*` (4) | pass |
| `serializeSession` / `deserializeSession` | new (not a `.m` port); Beam's own session file format | `SessionIo.*` (4) | pass |
| `countdownTimeLeftSeconds` / `isCountdownDone` / `countdownDisplayText` | `GUI/UIFeatures/startStandaloneCountdown.m`, `updateFigureTimer.m` | `CountdownTimeLeftSeconds.*`, `IsCountdownDone.*`, `CountdownDisplayText.*` (4 total) | pass |
| `centerArrayOnMri` | `GUI/initTransducers.m` (centering computation; `transformArrayDataToHFSRAS`/`defineTranslateAffineMatrix` inlined, see header comment) | `CenterArrayOnMri.PlacesArrayAtMriCenterPlusFixedOffset` | pass |
| `prepareShamSonication` | `Stimulation/GeneralSonication/unfocusedSonicate.m` (duration + `setShamAudio` orchestration) | `PrepareShamSonication.ComputesDurationAndDelegatesToSetShamAudio` | pass |
| `initTreatmentProtocols` | `GUI/SonicationTab/initTreatmentProtocolTables.m` (fresh-init branch) | `InitTreatmentProtocols.OneSessionPerProtocolWithBlankRows` | pass |
| `addTreatmentProtocolSession` | `GUI/SonicationTab/addTreatmentProtocolSession.m` | `AddTreatmentProtocolSession.AppendsNextVisitNumberUntilCapThenNoOps` | pass |
| `currentSessionRows` / `setCurrentSessionRows` / `findProtocol` / `findSession` | `GUI/SonicationTab/setTreatmentProtocolTableDisplay.m`, `setTreatmentProtocolTableData.m` (write-back half) | `TreatmentSessionStore.*` (2) | pass |
| `correctionInitialState` | `GUI/CorrectionTab/initializeCorrectionValues.m` (literal startup defaults) | `CorrectionInitialState.MatchesInitializeCorrectionValuesLiterals` | pass |
| `stimParamsFromTableRow` | `GUI/SonicationTab/setStimParamsFromApp.m` (per-row field copy) | `StimParamsFromTableRow.CopiesFieldsAndHardcodesCenterFrequency` | pass |
| `computeRegistrationCheckLampState` | `Registration/setRegistrationCheck.m` | `ComputeRegistrationCheckLampState.*` (4) | pass |
| `newProtocolName` | `GUI/SonicationTab/getNewProtocolName.m` | `NewProtocolName.*` (3) | pass |
| `exampleTargetHelpText` | `GUI/SonicationTab/ExampleTargets/setExampleTargetImages.m` (text-selection only) | `ExampleTargetHelpText.VimGetsItsOwnTextEverythingElseGetsTheDefault` | pass |
| `defaultArrayFramePosition` | `GUI/initArrayFramePosition.m` | `DefaultArrayFramePosition.StartsAtSliderMinimumNoShift` | pass |

## libs/gui_qt

| Function | Source | Test | Result |
|---|---|---|---|
| `SafetyReportView` | `GUI/Safety/checkSonicationSafety.m` (`SystemStatusTextArea`) | `apps/beam_app --check` | pass (smoke — Qt widget) |
| `buildTransmissionBarChart` / `buildRfWaveformChart` | `GUI/CorrectionTab/setAvgTransmissionDataBars.m`, `updateAttenuationPlots.m` | `apps/beam_app --check` | pass (smoke — Qt charts) |
| `buildPulseWaveformChart` | `GUI/SonicationTab/updateSonicationPlots.m` (pulse-plot segment) | `apps/beam_app --check` | pass (smoke — Qt chart) |
| `buildSonicationTimelineChart` / `setSonicationTimelineChart` | new (not a `.m` port); consumes tested `computeSonicationEventTimeline` | `apps/beam_app --check` (`timelineSegments=39`) | pass (smoke — Qt chart) |
| `BeamMainWindow` | `BeamV0.mlapp` TabGroup (shell) | `apps/beam_app --check` | pass (smoke — Qt tabs) |
| `FiducialTableModel` | `GUI/RegistrationTab/setFiducialROIs.m` (live path) | `apps/beam_app --check` (`fiducials=6`) | pass (smoke — Qt model; wraps tested `setArrayFiducialMarkers`) |
| `MriSliceView` / `renderMriSliceImage` | new (not a `.m` port); consumes already-ported `MRI/getSliceImage.m` | `apps/beam_app --check` (`mriAxialSlice=64x64`, real-file smoke via `--mri`) | pass (smoke — Qt image rendering) |
| `MriSliceView::setOverlayVolumes` / `renderMriSliceImageWithOverlay` | new (not a `.m` port); consumes tested `rasterizeArrayOntoMriGrid`/`rasterizeFiducialMarkersOntoMriGrid`/`rasterizeFocusEllipsoidOntoMriGrid` | `apps/beam_app --check --mri <file>` (`overlay arrayVoxels=90 fiducialVoxels=630 focusCoverage=2404.00`) | pass (smoke — Qt image rendering) |
| `MriSliceView::setAxes`/`paintEvent` (axis tick labels) | `GUI/drawMrImages.m`'s `xdata`/`ydata` per plane, plus the sagital-only `XDir`='reverse' mirror (`updateImage()`'s `QImage::mirrored`) | interactive run with `--mri`, compared side-by-side against a live BeamV0 session's real tick values and sagittal orientation | pass (visual — not unit-testable Qt paint output; see `docs/known_gaps_gui.md`) |
| `StimParamTableModel` | `GUI/SonicationTab/createStimParamTable.m`, `setStimParamTableNames.m` | `apps/beam_app --check` | pass (smoke — Qt model; wraps tested `sortSonicationTableOrder`/`getCurrentShownSonication`) |
| `TreatmentProtocolTableModel` | `GUI/SonicationTab/createTreatmentProtocolParamTable.m`, `setTreatmentProtocolTableNames.m` | `apps/beam_app --check` | pass (smoke — Qt model; wraps tested `getResponseFromTreatmentProtocolData`/`getTopTargetsFromTreatmentProtocolTable`) |
| `StimParamTableModel::addSonicationRow`/`removeMarkedRows` | `GUI/SonicationTab/createSonicationUpdateTable.m`, `removeSonicationUpdateTable.m` (both sources have zero real callers, ever -- reclassified Excluded 2026-09-12, see `docs/known_gaps_gui.md`; this port's own "Add Sonication"/"Remove Marked" buttons are real and unaffected, just not translating a live BeamV0 feature) | `apps/beam_app --check` (`rowMgmt 3->4->3`) | pass (smoke — Qt model) |
| `BeamMainWindow::setSonicateHandler`/`setSonicationStatus` | new (not a `.m` port); wraps tested `prepareSonication` + ported `setSerialCommandFromStimParams`/`sendSerialCommand` | `apps/beam_app --check` (`sonicate started=1 dutyCycle=0.676 waitForTrigger=0`) | pass (smoke — Qt widgets; real serial send untestable without hardware) |
| `BeamMainWindow::setShamHandler` ("Sham") | `Stimulation/GeneralSonication/unfocusedSonicate.m`; wraps tested `prepareShamSonication` | `apps/beam_app --check` (`shamAudioLen=441000`) | pass (smoke — Qt widgets; no hardware dependency, matching the source) |
| `BeamMainWindow` treatment protocol/visit selector + "New Visit" | `GUI/SonicationTab/`'s `TreatmentProtocolDropDown`/`VisitNumberListBox`/`NewVisitButtonPushed`; wraps tested `initTreatmentProtocols`/`addTreatmentProtocolSession`/`currentSessionRows`/`setCurrentSessionRows` | `apps/beam_app --check` (`treatment editPersisted=1 newVisitAdded=1 firstVisitUntouched=1`) | pass (smoke — Qt widgets + real `setData`-driven `dataChanged` signal) |
| `BeamMainWindow::startSonicationCountdown`/`sonicationCountdownText` | `GUI/UIFeatures/startStandaloneCountdown.m`/`updateFigureTimer.m`; wraps tested `countdownTimeLeftSeconds`/`isCountdownDone`/`countdownDisplayText` | `apps/beam_app --check` (`countdownTextOk=1`, comparing the real QLabel text to the presenter's output) | pass (smoke — QTimer mechanics) |
| `BeamMainWindow::collectSessionData`/`applySessionData` (File -> Save/Load Session) | new (not a `.m` port); wraps tested `serializeSession`/`deserializeSession` | `apps/beam_app --check` (`session roundTrip=1`, through a real temp file) | pass (smoke — Qt models + real file I/O; clicking the menu items / QFileDialog itself untested) |
| `BeamMainWindow::setRegisterHandler`/`setRegisterStatus` ("Register To MRI Fiducials") | new (not a `.m` port); wraps tested `registerArrayToFiducials` | `apps/beam_app --check` (`register movedArray=1`) | pass (smoke — Qt widgets; exercises the same code path as the button click) |
| `BeamMainWindow::setRegisterCurrentPositionHandler` ("Register Arrays to Current Position") | `registerCurrentTransducerPostion.m`; wraps tested `registerCurrentTransducerPosition` | `apps/beam_app --check` (`currentPositionMoved=1`) | pass (smoke — Qt widgets/sliders) |
| `BeamMainWindow::setRunCorrectionHandler`/`setCorrectionStatus` ("Run Correction") | `Correction/getRecieveWaveformFromSerial.m`; wraps tested `parseCorrectionWaveformLine`/`splitAndFilterReceiveWaveform` | `apps/beam_app --check` (`correction ch0Len=401 ch1Len=402`) | pass (smoke — Qt widgets; real serial read untestable without hardware) |
| `BeamMainWindow::setRegistrationCheckLampState` (3 lamps + Sonicate enable) | `Registration/setRegistrationCheck.m`; wraps tested `computeRegistrationCheckLampState` | `apps/beam_app --check` (`registrationCheckSonicateEnabled=1` after both registration buttons fire) | pass (smoke — Qt widgets; lamp colors themselves untestable headlessly) |

## Gaps

Every converted function has at least an indirect test. The only
device-dependent paths — `SerialPort`'s successful open + read/write and
`listAvailableComPorts`' actual enumeration — are exercised only for their
error / smoke behaviour here; a real port is needed to test the happy path.
