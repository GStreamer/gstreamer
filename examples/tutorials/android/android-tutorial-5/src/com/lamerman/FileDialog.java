// Based on http://code.google.com/p/android-file-dialog/
//
// Copyright (c) 2011, 2012, Alexander Ponomarev <alexander.ponomarev.1@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this list
// of conditions and the following disclaimer. Redistributions in binary form must
// reproduce the above copyright notice, this list of conditions and the following
// disclaimer in the documentation and/or other materials provided with the distribution.
// Neither the name of the <ORGANIZATION> nor the names of its contributors may be used
// to endorse or promote products derived from this software without specific prior
// written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
// SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.

package com.lamerman;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.TreeMap;

import android.app.AlertDialog;
import android.app.ListActivity;
import android.content.DialogInterface;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.widget.ListView;
import android.widget.SimpleAdapter;
import android.widget.TextView;

import org.freedesktop.gstreamer.tutorials.tutorial_5.R;

/**
 * Activity para escolha de arquivos/diretorios.
 *
 * @author android
 *
 */
public class FileDialog extends ListActivity {

    /**
     * Chave de um item da lista de paths.
     */
    private static final String ITEM_KEY = "key";

    /**
     * Imagem de um item da lista de paths (diretorio ou arquivo).
     */
    private static final String ITEM_IMAGE = "image";

    /**
     * Diretorio raiz.
     */
    private static final String ROOT = "/";

    /**
     * Parametro de entrada da Activity: path inicial. Padrao: ROOT.
     */
    public static final String START_PATH = "START_PATH";

    /**
     * Parametro de entrada da Activity: filtro de formatos de arquivos. Padrao:
     * null.
     */
    public static final String FORMAT_FILTER = "FORMAT_FILTER";

    /**
     * Parametro de saida da Activity: path escolhido. Padrao: null.
     */
    public static final String RESULT_PATH = "RESULT_PATH";

    private List<String> path = null;
    private TextView myPath;
    private ArrayList<HashMap<String, Object>> mList;

    private Button selectButton;

    private String parentPath;
    private String currentPath = ROOT;

    private String[] formatFilter = null;

    private File selectedFile;
    private HashMap<String, Integer> lastPositions = new HashMap<String, Integer>();

    /**
     * Called when the activity is first created. Configura todos os parametros
     * de entrada e das VIEWS..
     */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setResult(RESULT_CANCELED, getIntent());

        setContentView(R.layout.file_dialog_main);
        myPath = (TextView) findViewById(R.id.path);

        selectButton = (Button) findViewById(R.id.fdButtonSelect);
        selectButton.setEnabled(false);
        selectButton.setOnClickListener(new OnClickListener() {

            public void onClick(View v) {
                if (selectedFile != null) {
                    getIntent().putExtra(RESULT_PATH, selectedFile.getPath());
                    setResult(RESULT_OK, getIntent());
                    finish();
                }
            }
        });

        formatFilter = getIntent().getStringArrayExtra(FORMAT_FILTER);

        final Button cancelButton = (Button) findViewById(R.id.fdButtonCancel);
        cancelButton.setOnClickListener(new OnClickListener() {

            public void onClick(View v) {
                setResult(RESULT_CANCELED);
                finish();
            }

        });

        String startPath;
        if (savedInstanceState != null) {
            startPath = savedInstanceState.getString("currentPath");
        } else {
            startPath = getIntent().getStringExtra(START_PATH);
        }
        startPath = startPath != null ? startPath : ROOT;
        getDir(startPath);

        ListView lv = (ListView) findViewById(android.R.id.list);
        lv.setChoiceMode(ListView.CHOICE_MODE_SINGLE);
    }

    private void getDir(String dirPath) {

        boolean useAutoSelection = dirPath.length() < currentPath.length();

        Integer position = lastPositions.get(parentPath);

        getDirImpl(dirPath);

        if (position != null && useAutoSelection) {
            getListView().setSelection(position);
        }

    }

    /**
     * Monta a estrutura de arquivos e diretorios filhos do diretorio fornecido.
     *
     * @param dirPath
     *            Diretorio pai.
     */
    private void getDirImpl(final String dirPath) {

        currentPath = dirPath;

        final List<String> item = new ArrayList<String>();
        path = new ArrayList<String>();
        mList = new ArrayList<HashMap<String, Object>>();

        File f = new File(currentPath);
        File[] files = f.listFiles();
        if (files == null) {
            currentPath = ROOT;
            f = new File(currentPath);
            files = f.listFiles();
        }
        myPath.setText(getText(R.string.location) + ": " + currentPath);

        if (!currentPath.equals(ROOT)) {

            item.add(ROOT);
            addItem(ROOT, R.drawable.folder);
            path.add(ROOT);

            item.add("../");
            addItem("../", R.drawable.folder);
            path.add(f.getParent());
            parentPath = f.getParent();

        }

        TreeMap<String, String> dirsMap = new TreeMap<String, String>();
        TreeMap<String, String> dirsPathMap = new TreeMap<String, String>();
        TreeMap<String, String> filesMap = new TreeMap<String, String>();
        TreeMap<String, String> filesPathMap = new TreeMap<String, String>();
        for (File file : files) {
            if (file.isDirectory()) {
                String dirName = file.getName();
                dirsMap.put(dirName, dirName);
                dirsPathMap.put(dirName, file.getPath());
            } else {
                final String fileName = file.getName();
                final String fileNameLwr = fileName.toLowerCase();
                // se ha um filtro de formatos, utiliza-o
                if (formatFilter != null) {
                    boolean contains = false;
                    for (int i = 0; i < formatFilter.length; i++) {
                        final String formatLwr = formatFilter[i].toLowerCase();
                        if (fileNameLwr.endsWith(formatLwr)) {
                            contains = true;
                            break;
                        }
                    }
                    if (contains) {
                        filesMap.put(fileName, fileName);
                        filesPathMap.put(fileName, file.getPath());
                    }
                    // senao, adiciona todos os arquivos
                } else {
                    filesMap.put(fileName, fileName);
                    filesPathMap.put(fileName, file.getPath());
                }
            }
        }
        item.addAll(dirsMap.tailMap("").values());
        item.addAll(filesMap.tailMap("").values());
        path.addAll(dirsPathMap.tailMap("").values());
        path.addAll(filesPathMap.tailMap("").values());

        SimpleAdapter fileList = new SimpleAdapter(this, mList,
                R.layout.file_dialog_row,
                new String[] { ITEM_KEY, ITEM_IMAGE }, new int[] {
                        R.id.fdrowtext, R.id.fdrowimage });

        for (String dir : dirsMap.tailMap("").values()) {
            addItem(dir, R.drawable.folder);
        }

        for (String file : filesMap.tailMap("").values()) {
            addItem(file, R.drawable.file);
        }

        fileList.notifyDataSetChanged();

        setListAdapter(fileList);

    }

    private void addItem(String fileName, int imageId) {
        HashMap<String, Object> item = new HashMap<String, Object>();
        item.put(ITEM_KEY, fileName);
        item.put(ITEM_IMAGE, imageId);
        mList.add(item);
    }

    /**
     * Quando clica no item da lista, deve-se: 1) Se for diretorio, abre seus
     * arquivos filhos; 2) Se puder escolher diretorio, define-o como sendo o
     * path escolhido. 3) Se for arquivo, define-o como path escolhido. 4) Ativa
     * botao de selecao.
     */
    @Override
    protected void onListItemClick(ListView l, View v, int position, long id) {

        File file = new File(path.get(position));

        if (file.isDirectory()) {
            selectButton.setEnabled(false);
            if (file.canRead()) {
                lastPositions.put(currentPath, position);
                getDir(path.get(position));
            } else {
                new AlertDialog.Builder(this)
                        .setIcon(android.R.drawable.stat_sys_warning)
                        .setTitle(
                                "[" + file.getName() + "] "
                                        + getText(R.string.cant_read_folder))
                        .setPositiveButton("OK",
                                new DialogInterface.OnClickListener() {

                                    public void onClick(DialogInterface dialog,
                                            int which) {

                                    }
                                }).show();
            }
        } else {
            if (selectedFile != null
                    && selectedFile.getPath().equals(file.getPath())) {
                getIntent().putExtra(RESULT_PATH, selectedFile.getPath());
                setResult(RESULT_OK, getIntent());
                finish();
            }
            selectedFile = file;
            l.setItemChecked(position, true);
            selectButton.setEnabled(true);
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if ((keyCode == KeyEvent.KEYCODE_BACK)) {
            selectButton.setEnabled(false);

            if (!currentPath.equals(ROOT)) {
                getDir(parentPath);
            } else {
                return super.onKeyDown(keyCode, event);
            }

            return true;
        } else {
            return super.onKeyDown(keyCode, event);
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        outState.putString("currentPath", currentPath);
        super.onSaveInstanceState(outState);
    }

}
