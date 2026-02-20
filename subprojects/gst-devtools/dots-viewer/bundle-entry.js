import * as d3 from 'd3';
import 'd3-graphviz';
import FuseModule from 'fuse.js';
import { Graphviz } from '@hpcc-js/wasm/graphviz';
import { GstCaps, GstStructure, unwrapValue, valueToStringBare } from 'gststructure';

window.d3 = d3;
window.Fuse = FuseModule;
window.Graphviz = Graphviz;
window.GstCaps = GstCaps;
window.GstStructure = GstStructure;
window.unwrapValue = unwrapValue;
window.valueToStringBare = valueToStringBare;
