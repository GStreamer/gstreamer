// Import all dependencies first
import $ from 'jquery';
import 'jquery-mousewheel';
import 'jquery-color';
import 'bootstrap';
import 'dragscroll';
import * as vizModule from '@viz-js/viz';
import FuseModule from 'fuse.js';

// Make required libraries available globally
window.$ = window.jQuery = $;
window.viz = vizModule;
window.instance = vizModule.instance;
window.Fuse = FuseModule;


// Import Bootstrap CSS
import 'bootstrap/dist/css/bootstrap.min.css';
