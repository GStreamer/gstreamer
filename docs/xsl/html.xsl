<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'
                xmlns="http://www.w3.org/TR/xhtml1/transitional"
                exclude-result-prefixes="#default">

<!-- This file is for customizing the default XSL stylesheets. -->
<!-- We include them here (this one is for HTML output):       -->
<xsl:import
 href="http://docbook.sourceforge.net/release/xsl/current/xhtml/chunk.xsl"/>

<!-- .. and customize them here:                               -->
<xsl:include href="fileext.xsl"/>
<xsl:include href="keycombo.xsl"/>
<!-- <xsl:include href="admon.xsl"/> -->
<xsl:include href="css.xsl"/>

<xsl:param name="generate.component.toc" select="0"/>

</xsl:stylesheet>
