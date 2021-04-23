from PostProcessingBase import FlatNtupleBase

import os, re, sys, time, subprocess, math, json
import multiprocessing
from datetime import datetime
import hashlib
from ROOT import Math

class FlatNtupleForMuonMVA(FlatNtupleBase):
    """Flat ROOT ntuple producer for Bmm5 Muon MVA"""

    def _validate_inputs(self):
        """Task specific input validation"""

        # check for missing information
        for parameter in ['input', 'tree_name']:
            if parameter not in self.job_info:
                raise Exception("Missing input '%s'" % parameter)

    def _get_jpsis(self):
        """Get clean jpsi from jpsik events to be used as a control region"""

        candidates = []
        # for tag in range(self.event.nMuon):
        #     if self.event.Muon_pt[tag] < 4.0: continue
        #     if not self.event.Muon_isTracker[tag]: continue
        #     if not self.event.Muon_isGlobal[tag]: continue
        #     if not self.event.Muon_softMvaId[tag]: continue
        #     for probe in range(self.event.nMuon):
        #         if self.event.Muon_pt[probe] < 4.0: continue
        #         if tag == probe: continue
        #         if self.event.Muon_charge[tag] == self.event.Muon_charge[probe]:
        #             continue
        #         if not self.event.Muon_isTracker[probe]: continue
        #         if not self.event.Muon_isGlobal[probe]: continue
        #         p_tag   = Math.PtEtaPhiMVector(self.event.Muon_pt[tag],
        #                                        self.event.Muon_eta[tag],
        #                                        self.event.Muon_phi[tag],
        #                                        self.event.Muon_mass[tag])
        #         p_probe = Math.PtEtaPhiMVector(self.event.Muon_pt[probe],
        #                                        self.event.Muon_eta[probe],
        #                                        self.event.Muon_phi[probe],
        #                                        self.event.Muon_mass[probe])
        #         mass = (p_tag + p_probe).M()
        #         if abs(mass-3.1) > 0.1: continue
        #         candidates.append((probe, mass))

        for index in range(self.event.nbkmm):
            if self.event.bkmm_jpsimc_sl3d[index] > 4 and \
               self.event.bkmm_jpsimc_vtx_chi2dof[index] < 5 and \
               abs(self.event.bkmm_nomc_mass[index] - 5.29) < 0.05 and \
               abs(self.event.mm_kin_mass[self.event.bkmm_mm_index[index]] - 3.1) < 0.1:
                candidates.append((self.event.mm_mu1_index[self.event.bkmm_mm_index[index]],
                                   self.event.mm_kin_mass[self.event.bkmm_mm_index[index]]))
                candidates.append((self.event.mm_mu2_index[self.event.bkmm_mm_index[index]],
                                   self.event.mm_kin_mass[self.event.bkmm_mm_index[index]]))
        
        return candidates
            
    def _process_events(self):
        """Event loop"""

        for event_index, event in enumerate(self.input_tree):
            if self.limit > 0 and event_index >= self.limit: break
            
            self.event = event
            jpsi_candidates = self._get_jpsis()
            
            candidates = []

            for i in range(self.event.nMuon):
                if not self.event.Muon_isTracker[i]: continue
                if not self.event.Muon_isGlobal[i]: continue

                keep_muon = False
                # We should use only muons from known sources, because 
                # otherwise we can mix signal and background muons from pileup
                # We won't keep muons that we cannot identified, because
                # they can be of any type like it is

                data = dict()

                # MC specific requirements

                # Sim-based matching
                if hasattr(self.event, 'MuonId_simPdgId'):
                    if abs(self.event.MuonId_simPdgId[i]) == 13:
                        data['sim_pdgId']  = self.event.MuonId_simPdgId[i]
                        data['sim_mpdgId'] = self.event.MuonId_simMotherPdgId[i]
                        if self.event.MuonId_simType[i] == 2:
                            keep_muon = True
                            data['sim_type'] = 1
                        if self.event.MuonId_simType[i] == 3:
                            keep_muon = True
                            data['sim_type'] = 2
                            if abs(self.event.MuonId_simMotherPdgId[i]) in [443]:
                                data['sim_type'] = 3

                # Gen-based matching
                if not keep_muon and hasattr(self.event, 'Muon_genPartFlav'):
                    genPartFlav = ord(self.event.Muon_genPartFlav[i])
                    if genPartFlav == 3:
                        keep_muon = True
                        data['sim_type'] = 1
                    elif genPartFlav in [4, 5]:
                        keep_muon = True
                        data['sim_type'] = 2
                        
                # Add Jpsi muons
                for iprobe, mass in jpsi_candidates:
                    if i == iprobe:
                        data['jpsi_mass'] = mass
                        keep_muon = True
                        
                if keep_muon:
                    self._fill_tree(i, data)

    def _configure_output_tree(self):
        ## event info
        self.tree.addBranch('run',         'UInt_t', 0)
        self.tree.addBranch('evt',      'ULong64_t', 0)
        self.tree.addBranch('ls',          'UInt_t', 0)

        self.tree.addBranch('pt',         'Float_t', 0)
        self.tree.addBranch('eta',        'Float_t', 0)
        self.tree.addBranch('phi',        'Float_t', 0)

        self.tree.addBranch('bmmBaseId',   'UInt_t', 0)
        self.tree.addBranch('looseId',     'UInt_t', 0)
        self.tree.addBranch('mediumId',    'UInt_t', 0)
        self.tree.addBranch('tightId',     'UInt_t', 0)
        self.tree.addBranch('softId',      'UInt_t', 0)
        self.tree.addBranch('softMvaId',   'UInt_t', 0)
        self.tree.addBranch('softMva',    'Float_t', 0)

        self.tree.addBranch('trkKink',             'Float_t', 0, "Inner track kink chi2")
        self.tree.addBranch('glbTrackProbability', 'Float_t', 0, "Log probability of the global fit")
        self.tree.addBranch('chi2LocalPosition',   'Float_t', 0, "chi2 for STA-TK matching by local position")
        self.tree.addBranch('glbNormChi2',         'Float_t', 0, "Normalized chi2 of the global fit")
        self.tree.addBranch('trkValidFrac',        'Float_t', 0, "Fraction of valid hits for inner track")
        self.tree.addBranch('match1_dX',           'Float_t', 0, "Station 1 local segment-track dX")
        self.tree.addBranch('match1_pullX',        'Float_t', 0, "Station 1 local segment-track dX/dErr")
        self.tree.addBranch('match1_pullDxDz',     'Float_t', 0, "Station 1 local segment-track direction matching in x")
        self.tree.addBranch('match1_dY',           'Float_t', 0, "Station 1 local segment-track dY")
        self.tree.addBranch('match1_pullY',        'Float_t', 0, "Station 1 local segment-track dY/dErr")
        self.tree.addBranch('match1_pullDyDz',     'Float_t', 0, "Station 1 local segment-track direction matching in y")
        self.tree.addBranch('match2_dX',           'Float_t', 0, "Station 2 local segment-track dX")
        self.tree.addBranch('match2_pullX',        'Float_t', 0, "Station 2 local segment-track dX/dErr")
        self.tree.addBranch('match2_pullDxDz',     'Float_t', 0, "Station 2 local segment-track direction matching in x")
        self.tree.addBranch('match2_dY',           'Float_t', 0, "Station 2 local segment-track dY")
        self.tree.addBranch('match2_pullY',        'Float_t', 0, "Station 2 local segment-track dY/dErr")
        self.tree.addBranch('match2_pullDyDz',     'Float_t', 0, "Station 2 local segment-track direction matching in y")

        self.tree.addBranch('nPixels',              'UInt_t', 0, "Number of valid pixel hits")
        self.tree.addBranch('nValidHits',           'UInt_t', 0, "Number of valid hits")
        self.tree.addBranch('nLostHitsInner',       'UInt_t', 0, "Number of lost hits before tracker track")
        self.tree.addBranch('nLostHitsOn',          'UInt_t', 0, "Number of lost hits on tracker track")
        self.tree.addBranch('nLostHitsOuter',       'UInt_t', 0, "Number of lost hits after tracker track")

        self.tree.addBranch('trkLayers',            'UInt_t', 0, "Number of layers with measurements in tracker track")
        self.tree.addBranch('trkLostLayersInner',   'UInt_t', 0, "Number of lost layers befor tracker track")
        self.tree.addBranch('trkLostLayersOn',      'UInt_t', 0, "Number of lost layers on tracker track")
        self.tree.addBranch('trkLostLayersOuter',   'UInt_t', 0, "Number of lost layers after tracker track")

        self.tree.addBranch('highPurity',           'UInt_t', 0, "High purity inner track")
        
        self.tree.addBranch('sim_type',             'UInt_t', 0, "1 - decay in flight, 2 - heavy flavor decays, 3 - jpsi")
        self.tree.addBranch('sim_pdgId',             'Int_t', 0, "PDG id of SIM match")
        self.tree.addBranch('sim_mpdgId',            'Int_t', 0, "PDG id of SIM mother")
        self.tree.addBranch('jpsi_mass',           'Float_t', 0, "Jpsi mass for efficiency estimation")
        
        self.tree.addBranch('HLT_DoubleMu4_3_Jpsi',           'UInt_t', 0)
        self.tree.addBranch('HLT_DoubleMu4_Jpsi_Displaced',   'UInt_t', 0)
        self.tree.addBranch('HLT_DoubleMu4_Jpsi_NoVertexing', 'UInt_t', 0)
        self.tree.addBranch('HLT_DoubleMu4_3_Jpsi_Displaced', 'UInt_t', 0)
        self.tree.addBranch('HLT_Dimuon6_Jpsi_NoVertexing',   'UInt_t', 0)
        self.tree.addBranch('trigger',                        'UInt_t', 0, "OR of all relevant HLT Jpsi triggers")


    def _fill_tree(self, i, data):
        self.tree.reset()

        ## event info
        self.tree['run'] = self.event.run
        self.tree['ls']  = self.event.luminosityBlock
        self.tree['evt'] = self.event.event

        self.tree['pt']     = self.event.Muon_pt[i]
        self.tree['eta']    = self.event.Muon_eta[i]
        self.tree['phi']    = self.event.Muon_phi[i]

        self.tree['bmmBaseId']  = 0
        if self.event.Muon_isTracker[i] and self.event.Muon_isTracker[i] and \
           self.event.MuonId_highPurity[i] and self.event.Muon_pt[i] > 4.0 and \
           abs(self.event.Muon_eta[i]) < 1.4:
            self.tree['bmmBaseId']  = 1
        
        self.tree['looseId']    = self.event.Muon_looseId[i]
        self.tree['mediumId']   = self.event.Muon_mediumId[i]
        self.tree['tightId']    = self.event.Muon_tightId[i]
        self.tree['softId']     = self.event.Muon_softId[i]
        self.tree['softMvaId']  = self.event.Muon_softMvaId[i]

        self.tree['softMva']  = self.event.Muon_softMva[i]
                  
        self.tree['trkKink']             = self.event.MuonId_trkKink[i]
        self.tree['glbTrackProbability'] = self.event.MuonId_glbTrackProbability[i]
        self.tree['chi2LocalPosition']   = self.event.MuonId_chi2LocalPosition[i]
        self.tree['glbNormChi2']         = self.event.MuonId_glbNormChi2[i]
        self.tree['trkValidFrac']        = self.event.MuonId_trkValidFrac[i]
        self.tree['match1_dX']           = self.event.MuonId_match1_dX[i]
        self.tree['match1_pullX']        = self.event.MuonId_match1_pullX[i]
        self.tree['match1_pullDxDz']     = self.event.MuonId_match1_pullDxDz[i]
        self.tree['match1_dY']           = self.event.MuonId_match1_dY[i]
        self.tree['match1_pullY']        = self.event.MuonId_match1_pullY[i]
        self.tree['match1_pullDyDz']     = self.event.MuonId_match1_pullDyDz[i]
        self.tree['match2_dX']           = self.event.MuonId_match2_dX[i]
        self.tree['match2_pullX']        = self.event.MuonId_match2_pullX[i]
        self.tree['match2_pullDxDz']     = self.event.MuonId_match2_pullDxDz[i]
        self.tree['match2_dY']           = self.event.MuonId_match2_dY[i]
        self.tree['match2_pullY']        = self.event.MuonId_match2_pullY[i]
        self.tree['match2_pullDyDz']     = self.event.MuonId_match2_pullDyDz[i]

        self.tree['nPixels']             = self.event.MuonId_nPixels[i]
        self.tree['nValidHits']          = self.event.MuonId_nValidHits[i]
        self.tree['nLostHitsInner']      = self.event.MuonId_nLostHitsInner[i]
        self.tree['nLostHitsOn']         = self.event.MuonId_nLostHitsOn[i]
        self.tree['nLostHitsOuter']      = self.event.MuonId_nLostHitsOuter[i]

        self.tree['trkLayers']           = self.event.MuonId_trkLayers[i]
        self.tree['trkLostLayersInner']  = self.event.MuonId_trkLostLayersInner[i]
        self.tree['trkLostLayersOn']     = self.event.MuonId_trkLostLayersOn[i]
        self.tree['trkLostLayersOuter']  = self.event.MuonId_trkLostLayersOuter[i]

        self.tree['highPurity']          = self.event.MuonId_highPurity[i]

        # Trigger info
        trigger = 0
        if hasattr(self.event, 'HLT_DoubleMu4_3_Jpsi'):
            self.tree['HLT_DoubleMu4_3_Jpsi'] = self.event.HLT_DoubleMu4_3_Jpsi
            trigger |= self.event.HLT_DoubleMu4_3_Jpsi
        if hasattr(self.event, 'HLT_DoubleMu4_Jpsi_Displaced'):
            self.tree['HLT_DoubleMu4_Jpsi_Displaced'] = self.event.HLT_DoubleMu4_Jpsi_Displaced
            trigger |= self.event.HLT_DoubleMu4_Jpsi_Displaced
        if hasattr(self.event, 'HLT_DoubleMu4_Jpsi_NoVertexing'):
            self.tree['HLT_DoubleMu4_Jpsi_NoVertexing'] = self.event.HLT_DoubleMu4_Jpsi_NoVertexing
            trigger |= self.event.HLT_DoubleMu4_Jpsi_NoVertexing
        if hasattr(self.event, 'HLT_DoubleMu4_3_Jpsi_Displaced'):
            self.tree['HLT_DoubleMu4_3_Jpsi_Displaced'] = self.event.HLT_DoubleMu4_3_Jpsi_Displaced
            trigger |= self.event.HLT_DoubleMu4_3_Jpsi_Displaced
        if hasattr(self.event, 'HLT_Dimuon6_Jpsi_NoVertexing'):
            self.tree['HLT_Dimuon6_Jpsi_NoVertexing'] = self.event.HLT_Dimuon6_Jpsi_NoVertexing
            trigger |= self.event.HLT_Dimuon6_Jpsi_NoVertexing

        self.tree['trigger'] = trigger
            
        for key, value in data.items():
            self.tree[key] = value

        self.tree.fill()

if __name__ == "__main__":

    job = {
        "input": [
            "/eos/cms/store/group/phys_bphys/bmm/bmm5/NanoAOD/512/Charmonium+Run2018D-PromptReco-v2+MINIAOD/98841806-7910-3B4F-8818-832B7FFDC87B.root"
            # "/afs/cern.ch/work/d/dmytro/projects/RunII-NanoAODv6/src/BsToMuMu_bmm_fakes_and_ids.root"
        ],
        "tree_name" : "muons",
      }  

    file_name = "/tmp/dmytro/test.job"
    json.dump(job, open(file_name, "w"))

    p = FlatNtupleForMuonMVA(file_name)
    # p.limit = 1000
        
    p.process()
